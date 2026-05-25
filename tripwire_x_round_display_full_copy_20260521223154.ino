#include <TFT_eSPI.h>
#include <SPI.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <SD.h>
#include <FS.h>
#include "boot.h"
#include "WebUI.h"
#include "squirrel_egg.h"

// ======================================================
// CONSTANTS
// ======================================================

#define MAX_DEVICES        100
#define MAX_KNOWN_DEVICES  500
#define DEVICE_TIMEOUT_MS  12000
#define CLEANUP_TIMEOUT_MS 15000
#define SCAN_INTERVAL_MS   2500
#define DRAW_INTERVAL_MS   80
#define PULSE_INTERVAL_MS  120
#define TOUCH_DEBOUNCE_MS  150
#define MAX_LOG_SIZE       51200
#define SWEEP_STEP         5

#define RSSI_FLOOR    -100
#define RSSI_CEIL     -30
#define RSSI_STRONG   -60
#define RSSI_HIGH_RISK -55
#define RSSI_CRITICAL -45

// ======================================================
// DISPLAY
// ======================================================

TFT_eSPI tft = TFT_eSPI();

// ======================================================
// PINS (raw GPIO numbers — board-independent)
// ======================================================
// XIAO ESP32C5 mapping:
// D0=GPIO1, D1=GPIO0, D2=GPIO25, D3=GPIO7,
// D4=GPIO23(SDA), D5=GPIO24(SCL), D6=GPIO11(TX), D7=GPIO12(RX),
// D8=GPIO8(SCK), D9=GPIO9(MISO), D10=GPIO10(MOSI)

#define TOUCH_SDA 23   // D4
#define TOUCH_SCL 24   // D5
#define TOUCH_RST -1
#define TOUCH_IRQ 12   // D7

// Touch controller is CHSC6X on the Seeed Round Display (I2C addr 0x2E, INT pin driven LOW when pressed)
// No CST816S library needed — we poll the INT pin directly

#define BUZZER_PIN 26  // JST pin 2 (BAT_VOLT_PIN_EN — safe as buzzer output when not using batt monitoring)
#define SD_CS      1   // D0

// ======================================================
// MODES
// ======================================================

enum ScanMode { BLE_MODE, WIFI_MODE };
ScanMode currentMode = BLE_MODE;

enum AppState { APP_MENU, APP_BLE_SCAN, APP_WIFI_SCAN, APP_CONFIG, APP_ABOUT, APP_DEAUTH };
AppState appState = APP_MENU;
int menuIndex = 0;
const char* menuItems[] = {"BLE Scan", "WiFi Scan", "Config AP", "About"};
const int menuCount = 4;

// ======================================================
// BLE
// ======================================================

NimBLEScan* pBLEScan = nullptr;

// ======================================================
// SCAN RESULT QUEUE (producer: BLE callback / consumer: loop)
// ======================================================

#define RESULT_QUEUE_SIZE 32

struct ScanResult {
    char mac[18];
    char name[24];
    int8_t rssi;
};

ScanResult resultQueue[RESULT_QUEUE_SIZE];
volatile int resultQueueHead = 0;
volatile int resultQueueTail = 0;

// ======================================================
// DEVICE STRUCT
// ======================================================

struct DeviceInfo {
    char name[24];
    char mac[18];
    int8_t rssi;
    bool isNew;
    unsigned long firstSeen;
    unsigned long lastSeen;
};

// ======================================================
// STORAGE
// ======================================================

DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

char knownDevices[MAX_KNOWN_DEVICES][18];
int knownDeviceCount = 0;

bool newDeviceDetected = false;
int totalLogs = 0;
bool sdReady = false;

// ======================================================
// TARGET TRACKING
// ======================================================

int activeTargetIndex = -1;
unsigned long targetPulseTimer = 0;
int pulseSize = 0;

// ======================================================
// TIMERS
// ======================================================

int sweepAngle = 0;
int prevSweepX = 120, prevSweepY = 215; // initial sweep line endpoint
unsigned long lastScan = 0;
unsigned long alertTimer = 0;
unsigned long beepTimer = 0;
unsigned long lastTouchMs = 0;
static bool radarBgDrawn = false;

// Easter egg: tap 10 times to show secret image
#define EASTER_EGG_TAPS 10
static int easterTapCount = 0;
static unsigned long lastEasterTapMs = 0;
static int configTapCount = 0;

// Deauth easter egg (hold About screen)
static volatile unsigned long deauthCount = 0;
static volatile bool deauthDetected = false;
static char deauthLastSrc[18] = "none";
static bool deauthActive = false;
static unsigned long deauthFlashUntil = 0;
static bool deauthDrawn = false;
static int deauthChannel = 1;
static unsigned long deauthChanTimer = 0;

void deauthSnifferCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *frame = pkt->payload;
    if ((frame[0] & 0xFC) == 0xC0 || (frame[0] & 0xFC) == 0xA0) {
        deauthCount++;
        snprintf(deauthLastSrc, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
            frame[10], frame[11], frame[12], frame[13], frame[14], frame[15]);
        deauthDetected = true;
    }
}

// ======================================================
// BLE CALLBACK (lightweight — only fills ring buffer)
// ======================================================

class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
        int next = (resultQueueHead + 1) % RESULT_QUEUE_SIZE;
        if (next == resultQueueTail) return;

        std::string macStr = advertisedDevice->getAddress().toString();
        strncpy(resultQueue[resultQueueHead].mac, macStr.c_str(), 17);
        resultQueue[resultQueueHead].mac[17] = '\0';

        if (advertisedDevice->haveName()) {
            std::string n = advertisedDevice->getName();
            strncpy(resultQueue[resultQueueHead].name, n.c_str(), 23);
        } else {
            strncpy(resultQueue[resultQueueHead].name, "UNKNOWN", 23);
        }
        resultQueue[resultQueueHead].name[23] = '\0';

        resultQueue[resultQueueHead].rssi = advertisedDevice->getRSSI();

        resultQueueHead = next;
    }
};

// ======================================================
// BLE INIT TASK (non-blocking with timeout)
// ======================================================

static TaskHandle_t bleInitTask = NULL;
static volatile bool bleInitDone = false;
static volatile bool bleInitSuccess = false;
static unsigned long bleInitStartMs = 0;

void bleInitTaskFunc(void *pvParameters) {
    delay(10);
    NimBLEDevice::init("");
    pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setScanCallbacks(new MyScanCallbacks());
    pBLEScan->setActiveScan(true);
    bleInitSuccess = true;
    bleInitDone = true;
    vTaskDelete(NULL);
}

// ======================================================
// HELPERS
// ======================================================

bool isDeviceKnown(const char* mac) {
    for (int i = 0; i < knownDeviceCount; i++) {
        if (strcmp(knownDevices[i], mac) == 0) return true;
    }
    return false;
}

int findDeviceIndex(const char* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].mac, mac) == 0) return i;
    }
    return -1;
}

// ======================================================
// SD LOG ROTATION
// ======================================================

void checkLogRotation(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    bool tooBig = (f.size() > MAX_LOG_SIZE);
    f.close();
    if (tooBig) {
        SD.remove(path);
    }
}

void saveKnownDevice(const char* mac) {
    if (!sdReady || knownDeviceCount >= MAX_KNOWN_DEVICES) return;
    File file = SD.open("/known_devices.txt", FILE_APPEND);
    if (!file) return;
    file.println(mac);
    file.close();
}

void loadKnownDevices() {
    if (!sdReady) return;
    File file = SD.open("/known_devices.txt");
    if (!file) {
        Serial.println("NO DATABASE FOUND");
        return;
    }
    while (file.available() && knownDeviceCount < MAX_KNOWN_DEVICES) {
        String mac = file.readStringUntil('\n');
        mac.trim();
        if (mac.length() > 0) {
            strncpy(knownDevices[knownDeviceCount], mac.c_str(), 17);
            knownDevices[knownDeviceCount][17] = '\0';
            knownDeviceCount++;
        }
    }
    file.close();
    Serial.printf("DATABASE LOADED: %d entries\n", knownDeviceCount);
}

// ======================================================
// LOG DETECTION
// ======================================================

void logDetection(const DeviceInfo& dev) {
    if (!sdReady) return;

    checkLogRotation("/logs.txt");
    File file = SD.open("/logs.txt", FILE_APPEND);
    if (!file) return;

    const char* modeText = (currentMode == BLE_MODE) ? "BLE" : "WIFI";

    file.print("[");
    file.print(modeText);
    file.print("] ");
    file.print(millis());
    file.print(" | ");
    file.print(dev.mac);
    file.print(" | ");
    file.print(dev.name);
    file.print(" | RSSI:");
    file.println(dev.rssi);
    totalLogs++;
    file.close();

    if (dev.rssi > RSSI_CRITICAL) {
        checkLogRotation("/threats.txt");
        File threat = SD.open("/threats.txt", FILE_APPEND);
        if (threat) {
            threat.print("[HIGH] ");
            threat.print(dev.mac);
            threat.print(" | ");
            threat.print(dev.name);
            threat.print(" | RSSI:");
            threat.println(dev.rssi);
            threat.close();
        }
    }
}

// ======================================================
// ADD OR UPDATE DEVICE
// ======================================================

int addOrUpdateDevice(const char* mac, const char* name, int8_t rssi) {
    int idx = findDeviceIndex(mac);
    if (idx >= 0) {
        devices[idx].rssi = rssi;
        devices[idx].lastSeen = millis();
        return idx;
    }

    bool known = isDeviceKnown(mac);
    bool isNewDev = !known;

    if (isNewDev) {
        if (knownDeviceCount < MAX_KNOWN_DEVICES) {
            strncpy(knownDevices[knownDeviceCount], mac, 17);
            knownDevices[knownDeviceCount][17] = '\0';
            knownDeviceCount++;
        }
        saveKnownDevice(mac);
        newDeviceDetected = true;
        alertTimer = millis();
    }

    int slot = -1;

    if (deviceCount < MAX_DEVICES) {
        slot = deviceCount;
        deviceCount++;
    } else {
        // FIFO eviction — replace oldest
        unsigned long oldestTime = devices[0].lastSeen;
        slot = 0;
        for (int i = 1; i < MAX_DEVICES; i++) {
            if (devices[i].lastSeen < oldestTime) {
                oldestTime = devices[i].lastSeen;
                slot = i;
            }
        }
    }

    strncpy(devices[slot].mac, mac, 17);
    devices[slot].mac[17] = '\0';
    strncpy(devices[slot].name, name, 23);
    devices[slot].name[23] = '\0';
    devices[slot].rssi = rssi;
    devices[slot].isNew = isNewDev;
    devices[slot].firstSeen = millis();
    devices[slot].lastSeen = millis();

    if (isNewDev) {
        logDetection(devices[slot]);
    }

    return slot;
}

// ======================================================
// PROCESS QUEUED BLE RESULTS
// ======================================================

void processBLEResults() {
    while (resultQueueTail != resultQueueHead) {
        ScanResult& r = resultQueue[resultQueueTail];
        addOrUpdateDevice(r.mac, r.name, r.rssi);
        resultQueueTail = (resultQueueTail + 1) % RESULT_QUEUE_SIZE;
    }
}

// ======================================================
// UPDATE ACTIVE TARGET
// ======================================================

void updateTargetTracking() {
    activeTargetIndex = -1;
    int strongest = RSSI_FLOOR;

    for (int i = 0; i < deviceCount; i++) {
        if (millis() - devices[i].lastSeen > DEVICE_TIMEOUT_MS) continue;
        if (devices[i].rssi > strongest) {
            strongest = devices[i].rssi;
            activeTargetIndex = i;
        }
    }
}

// ======================================================
// RADAR DRAWING
// ======================================================

void drawRadarBg() {
    if (radarBgDrawn) return;
    radarBgDrawn = true;
    tft.fillScreen(TFT_BLACK);
    const int cx = 120, cy = 120;
    tft.drawCircle(cx, cy, 100, TFT_DARKGREEN);
    tft.drawCircle(cx, cy, 75, TFT_DARKGREEN);
    tft.drawCircle(cx, cy, 50, TFT_DARKGREEN);
    tft.drawCircle(cx, cy, 25, TFT_DARKGREEN);
    tft.drawLine(cx, 20, cx, 220, TFT_DARKGREEN);
    tft.drawLine(20, cy, 220, cy, TFT_DARKGREEN);
    prevSweepX = cx + 95;
    prevSweepY = cy;
}

void drawSweep() {
    const int cx = 120, cy = 120;

    // Erase old sweep (also erases crosshair pixels where they overlap)
    tft.drawLine(cx, cy, prevSweepX, prevSweepY, TFT_BLACK);

    // Restore crosshairs that were damaged by the erase
    tft.drawLine(cx, 20, cx, 220, TFT_DARKGREEN);
    tft.drawLine(20, cy, 220, cy, TFT_DARKGREEN);

    // Calculate new sweep position
    float rad = sweepAngle * 0.0174533f;
    int x = cx + (int)(cosf(rad) * 95.0f);
    int y = cy + (int)(sinf(rad) * 95.0f);

    tft.drawLine(cx, cy, x, y, TFT_GREEN);

    sweepAngle += SWEEP_STEP;
    if (sweepAngle >= 360) sweepAngle = 0;

    prevSweepX = x;
    prevSweepY = y;
}

void drawDevices() {
    for (int i = 0; i < deviceCount; i++) {
        const DeviceInfo& d = devices[i];

        if (millis() - d.lastSeen > DEVICE_TIMEOUT_MS) continue;

        int radius = map(d.rssi, RSSI_FLOOR, RSSI_CEIL, 90, 20);
        radius = constrain(radius, 20, 90);

        uint32_t hash = 0;
        for (int j = 0; d.mac[j]; j++) {
            hash = (hash * 31) + (unsigned char)d.mac[j];
        }
        int angle = hash % 360;
        float rad = angle * 0.0174533f;
        int x = 120 + (int)(cosf(rad) * radius);
        int y = 120 + (int)(sinf(rad) * radius);

        uint16_t colour = d.isNew ? TFT_CYAN : TFT_GREEN;
        if (d.rssi > RSSI_STRONG)   colour = TFT_YELLOW;
        if (d.rssi > RSSI_CRITICAL) colour = TFT_RED;
        if (i == activeTargetIndex) colour = TFT_MAGENTA;

        tft.fillCircle(x, y, 4, colour);
        tft.drawCircle(x, y, 7, colour);

        if (i == activeTargetIndex) {
            tft.drawCircle(x, y, 10 + pulseSize, TFT_MAGENTA);
            tft.drawCircle(x, y, 14 + pulseSize, TFT_MAGENTA);
        }

        if (d.rssi > RSSI_HIGH_RISK || i == activeTargetIndex) {
            tft.setTextSize(1);
            tft.setTextColor(colour);
            tft.setCursor(x + 8, y - 5);
            if (strcmp(d.name, "UNKNOWN") != 0) {
                char buf[9];
                strncpy(buf, d.name, 8);
                buf[8] = '\0';
                tft.print(buf);
            } else {
                tft.print("DEVICE");
            }
        }
    }
}

// ======================================================
// UI
// ======================================================

void drawUI() {
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(65, 15);
    tft.print(currentMode == BLE_MODE ? "BLE MODE" : "WIFI MODE");

    if (activeTargetIndex >= 0 && activeTargetIndex < deviceCount) {
        const DeviceInfo& t = devices[activeTargetIndex];

        tft.setTextSize(1);
        tft.setTextColor(TFT_MAGENTA);
        tft.setCursor(55, 38);
        tft.print("TARGET LOCK");

        tft.setCursor(45, 50);
        if (strcmp(t.name, "UNKNOWN") != 0) {
            char buf[13];
            strncpy(buf, t.name, 12);
            buf[12] = '\0';
            tft.print(buf);
        } else {
            tft.print("UNKNOWN DEVICE");
        }

        tft.setCursor(90, 62);
        tft.print(t.rssi);
        tft.print(" dBm");
    }

    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(70, 180);
    tft.printf("LOGS %d", totalLogs);

    tft.setCursor(70, 200);
    tft.printf("LIVE %d", deviceCount);

    tft.setCursor(70, 220);
    tft.printf("KNOWN %d", knownDeviceCount);

    tft.setCursor(150, 220);
    tft.print(currentMode == BLE_MODE ? "WIFI>" : "BLE>");

    if (!sdReady) {
        tft.setTextColor(TFT_RED);
        tft.setCursor(5, 5);
        tft.setTextSize(1);
        tft.print("NO SD");
    }

    if (newDeviceDetected && millis() - alertTimer < 3000) {
        tft.fillRoundRect(45, 95, 150, 35, 8, TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(58, 108);
        tft.setTextSize(1);
        tft.print("NEW DEVICE");
    }
}

// ======================================================
// GEIGER AUDIO
// ======================================================

void geigerSound() {
    if (activeTargetIndex < 0 || activeTargetIndex >= deviceCount) return;

    int rssi = devices[activeTargetIndex].rssi;
    int interval = map(rssi, RSSI_FLOOR, RSSI_CEIL, 1200, 40);
    int pitch    = map(rssi, RSSI_FLOOR, RSSI_CEIL, 1200, 4000);

    if (millis() - beepTimer > (unsigned long)interval) {
        tone(BUZZER_PIN, pitch, 20);
        beepTimer = millis();
    }
}

// ======================================================
// BLE SCAN (short duration to minimise AP disruption)
// ======================================================

// Async BLE scan state machine
static bool bleScanBusy = false;

void startBLEScan() {
    if (bleScanBusy) return;
    bleScanBusy = true;
    pBLEScan->start(300, false, true);
}

void processBLEScan() {
    if (pBLEScan->isScanning()) return;  // still in progress
    processBLEResults();
    pBLEScan->clearResults();
    bleScanBusy = false;
}

// ======================================================
// WIFI SCAN (async — non-blocking, preserves AP link)
// ======================================================

int wifiScanState = 0; // 0=idle, 1=bootstrapping, 2=in-progress

void startWiFiScan() {
    if (wifiScanState != 0) return;
    // Kick off async scan
    WiFi.scanNetworks(true, true);
    wifiScanState = 1;
}

void processWiFiScan() {
    int n = WiFi.scanComplete();
    if (n == -1) {
        wifiScanState = 2; // still in progress
        return;
    }
    if (n == -2) {
        wifiScanState = 0; // not started (shouldn't happen)
        return;
    }

    wifiScanState = 0;
    for (int i = 0; i < n; i++) {
        String ssid  = WiFi.SSID(i);
        String bssid = WiFi.BSSIDstr(i);
        int8_t rssi  = WiFi.RSSI(i);

        char mac[18], name[24];
        strncpy(mac, bssid.c_str(), 17);
        mac[17] = '\0';
        strncpy(name, ssid.c_str(), 23);
        name[23] = '\0';

        addOrUpdateDevice(mac, name, rssi);
    }
    WiFi.scanDelete();
}


// ======================================================
// PROCESS QUEUED BLE RESULTS
// ======================================================
// CLEANUP STALE DEVICES
// ======================================================

void cleanupDevices() {
    for (int i = deviceCount - 1; i >= 0; i--) {
        if (millis() - devices[i].lastSeen > CLEANUP_TIMEOUT_MS) {
            int moveCount = deviceCount - i - 1;
            if (moveCount > 0) {
                memmove(&devices[i], &devices[i + 1], moveCount * sizeof(DeviceInfo));
            }
            deviceCount--;
        }
    }
}

// ======================================================
// STATE FLAGS (file-scope so touch handler and loop can share)
// ======================================================

static bool firstMenuDraw = true;
static bool bleInitted = false;
static bool wifiInitted = false;
static bool webUIActive = false;
static unsigned long apStartTime = 0;
static bool apClientEverConnected = false;
static AppState lastAppState = APP_MENU;
static bool bleScanEntered = false;
static bool wifiScanEntered = false;
static bool configDrawn = false;
static bool aboutDrawn = false;
static unsigned long lastDrawTime = 0;
// ======================================================
// TOUCH HANDLER (CHSC6X: INT pin LOW = pressed, HIGH = released)
// ======================================================

static bool touching = false;
static unsigned long touchStartMs = 0;

void handleTouch() {
    // IRQ HIGH + not tracking = no activity, skip fast
    if (digitalRead(TOUCH_IRQ) == HIGH && !touching) return;
    if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return;

    // Read CHSC6X I2C to get actual touch state and acknowledge INT
    Wire.requestFrom(0x2E, 5);
    uint8_t buf[5] = {0};
    for (int i = 0; i < 5 && Wire.available(); i++) buf[i] = Wire.read();
    bool pressed = (buf[0] == 0x01); // byte 0 = touch status

    lastTouchMs = millis();

    if (pressed && !touching) {
        touching = true;
        touchStartMs = millis();
    } else if (!pressed && touching) {
        touching = false;
        unsigned long duration = millis() - touchStartMs;

        if (duration < 400) {
            if (millis() - lastEasterTapMs > 5000) { easterTapCount = 0; configTapCount = 0; }
            lastEasterTapMs = millis();
            tone(BUZZER_PIN, 3000, 15);
            if (appState == APP_CONFIG) {
                configTapCount++;
                if (configTapCount >= 7) {
                    configTapCount = 0;
                    tft.fillScreen(TFT_BLACK);
                    tft.pushImage(20, 20, SQUIRREL_EGG_WIDTH, SQUIRREL_EGG_HEIGHT, squirrel_egg);
                    tft.setTextSize(2);
                    tft.setTextColor(TFT_CYAN);
                    tft.setCursor(40, 215);
                    tft.print("SQUIRREL!");
                    delay(5000);
                    return;
                }
                return;
            }
            easterTapCount++;
            if (easterTapCount >= EASTER_EGG_TAPS) {
                easterTapCount = 0;
                drawEasterEgg();
                delay(5000);
                if (appState == APP_MENU) firstMenuDraw = true;
                else { radarBgDrawn = false; }
                return;
            }
            if (appState == APP_MENU) {
                menuIndex = (menuIndex + 1) % menuCount;
                firstMenuDraw = true;
            } else if (appState == APP_BLE_SCAN || appState == APP_WIFI_SCAN) {
                if (deviceCount > 0)
                    activeTargetIndex = (activeTargetIndex + 1) % deviceCount;
            }
        } else {
            easterTapCount = 0;
            configTapCount = 0;
            tone(BUZZER_PIN, 1500, 50);
            if (appState == APP_MENU) {
                switch (menuIndex) {
                    case 0: appState = APP_BLE_SCAN; currentMode = BLE_MODE; break;
                    case 1: appState = APP_WIFI_SCAN; currentMode = WIFI_MODE; break;
                    case 2: appState = APP_CONFIG; break;
                    case 3: appState = APP_ABOUT; break;
                }
            } else if (appState == APP_ABOUT) {
                appState = APP_DEAUTH;
            } else {
                appState = APP_MENU;
                menuIndex = 0;
            }
            tft.fillScreen(TFT_BLACK);
        }
    }
}

// ======================================================
// MENU / ABOUT DRAWING
// ======================================================

// ======================================================
// EASTER EGG
// ======================================================

void drawEasterEgg() {
    tft.fillScreen(TFT_BLACK);
    for (int y = 0; y < 240; y += 4) {
        tft.fillRect(0, y, 240, 2, tft.color565(y * 6 % 256, y * 3 % 256, 255 - y));
    }
    tft.fillCircle(120, 100, 50, TFT_BLACK);
    tft.fillCircle(120, 100, 48, tft.color565(255, 200, 0));
    tft.fillCircle(110, 90, 6, TFT_BLACK);
    tft.fillCircle(130, 90, 6, TFT_BLACK);
    tft.fillCircle(120, 105, 4, TFT_BLACK);
    tft.fillCircle(120, 110, 8, tft.color565(200, 0, 0));
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(35, 20);
    tft.print("EASTER EGG!");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(55, 180);
    tft.print("10 taps, nice!");
    tft.setCursor(25, 200);
    tft.print("now hold to return");
}

void drawMenu() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(42, 25);
    tft.print("TRIPWIRE");

    tft.setTextSize(1);
    for (int i = 0; i < menuCount; i++) {
        int y = 75 + i * 32;
        if (i == menuIndex) {
            tft.fillRoundRect(50, y - 4, 140, 22, 6, TFT_DARKGREEN);
            tft.setTextColor(TFT_BLACK);
            tft.setCursor(62, y);
            tft.print(menuItems[i]);
        } else {
            tft.setTextColor(TFT_GREEN);
            tft.setCursor(62, y);
            tft.print(menuItems[i]);
        }
    }

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(40, 218);
    tft.setTextSize(1);
    tft.print("TAP: NAV   HOLD: SELECT");
}

void drawAbout() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(45, 35);
    tft.print("TRIPWIRE");
    tft.setTextSize(1);
    tft.setCursor(25, 75);
    tft.print("BLE/WiFi Radar Scanner");
    tft.setCursor(25, 95);
    tft.print("XIAO ESP32C5");
    tft.setCursor(25, 125);
    tft.printf("Devices: %d", deviceCount);
    tft.setCursor(25, 145);
    tft.printf("Known: %d", knownDeviceCount);
    tft.setCursor(25, 165);
    tft.printf("Logs: %d", totalLogs);
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(25, 218);
    tft.print("HOLD TO RETURN");
}

// ======================================================
// DEAUTH DETECTOR SCREEN
// ======================================================

void drawDeauthScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(20, 30);
    tft.print("DEAUTH DETECTOR");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(20, 75);
    tft.print("Monitoring WiFi...");
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(20, 100);
    tft.print("Deauths:");
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(100, 100);
    tft.print("0");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(20, 125);
    tft.print("Last source:");
    tft.setTextColor(TFT_ORANGE);
    tft.setCursor(20, 145);
    tft.print("none");
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(20, 218);
    tft.print("HOLD TO RETURN");
}

// ======================================================
// SETUP
// ======================================================

void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] Starting...");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    pinMode(BUZZER_PIN, OUTPUT);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    pinMode(TOUCH_IRQ, INPUT_PULLUP); // CHSC6X INT: LOW = pressed, HIGH = released

    tft.init();
    tft.setRotation(0);

    // Boot screen
    tft.fillScreen(TFT_BLACK);
    tft.pushImage(0, 0, 240, 240, notorious_squirrel_boot_240x240_TRUE24);
    delay(2000);

    // SD card
    sdReady = SD.begin(SD_CS);
    if (!sdReady) {
        Serial.println("[BOOT] SD FAILED");
    } else {
        Serial.println("[BOOT] SD OK");
        loadKnownDevices();
    }

    Serial.println("[BOOT] Setup complete (waiting for deferred init)");
}



// ======================================================
// LOOP
// ======================================================

void loop() {
    unsigned long now = millis();

    // --- Touch ---
    handleTouch();

    // ========================================================
    // STATE TRANSITION: cleanup previous state, reset per-state flags
    // ========================================================
    if (appState != lastAppState) {
        if (lastAppState == APP_CONFIG && webUIActive) {
            stopWebUI();
            webUIActive = false;
        }
        if (lastAppState == APP_DEAUTH && deauthActive) {
            esp_wifi_set_promiscuous(false);
            deauthActive = false;
        }
        bleScanBusy = false;
        wifiScanState = 0;
        bleScanEntered = false;
        wifiScanEntered = false;
        configDrawn = false;
        aboutDrawn = false;
        deauthDrawn = false;
        bleInitted = false;
        bleInitDone = false;
        radarBgDrawn = false;
        lastAppState = appState;
        firstMenuDraw = true;
        if (bleInitTask != NULL) {
            vTaskDelete(bleInitTask);
            bleInitTask = NULL;
        }
    }

    // ========================================================
    // STATE MACHINE DISPATCH
    // ========================================================

    switch (appState) {

        // ------------------------------------------------
        // MAIN MENU
        // ------------------------------------------------
        case APP_MENU:
            if (firstMenuDraw) {
                drawMenu();
                firstMenuDraw = false;
            }
            break;

        // ------------------------------------------------
        // BLE SCAN MODE
        // ------------------------------------------------
        case APP_BLE_SCAN:
            if (!bleInitted) {
                bleInitted = true;
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_GREEN);
                tft.setTextSize(2);
                tft.setCursor(40, 100);
                tft.print("INIT BLE...");
                tft.setTextSize(1);
                tft.setCursor(40, 130);
                tft.print("(this may take a moment)");
                tft.setTextColor(TFT_DARKGREEN);
                tft.setCursor(40, 218);
                tft.print("HOLD TO CANCEL");
                bleInitDone = false;
                bleInitSuccess = false;
                bleInitStartMs = millis();
                xTaskCreate(bleInitTaskFunc, "bleInit", 4096, NULL, 1, &bleInitTask);
            }
            if (!bleInitDone) {
                if (millis() - bleInitStartMs > 10000) {
                    if (bleInitTask != NULL) {
                        vTaskDelete(bleInitTask);
                        bleInitTask = NULL;
                    }
                    bleInitted = false;
                    appState = APP_MENU;
                    tft.fillScreen(TFT_BLACK);
                    tft.setTextColor(TFT_RED);
                    tft.setTextSize(2);
                    tft.setCursor(15, 100);
                    tft.print("BLE INIT FAILED");
                    delay(1500);
                }
                break;
            }
            if (!bleScanEntered) {
                bleScanEntered = true;
                startBLEScan();
            }
            processBLEScan();
            if (!bleScanBusy && !scanningPaused && now - lastScan > SCAN_INTERVAL_MS) {
                startBLEScan();
                lastScan = now;
            }
            updateTargetTracking();
            if (now - targetPulseTimer > PULSE_INTERVAL_MS) {
                pulseSize = (pulseSize + 1) % 7;
                targetPulseTimer = now;
            }
            geigerSound();
            cleanupDevices();
            if (now - lastDrawTime > DRAW_INTERVAL_MS) {
                drawRadarBg();
                drawSweep();
                drawDevices();
                drawUI();
                lastDrawTime = now;
            }
            break;

        // ------------------------------------------------
        // WIFI SCAN MODE
        // ------------------------------------------------
        case APP_WIFI_SCAN:
            if (!wifiInitted) {
                wifiInitted = true;
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_GREEN);
                tft.setTextSize(2);
                tft.setCursor(40, 100);
                tft.print("INIT WiFi...");
                WiFi.mode(WIFI_STA);
                WiFi.disconnect();
                delay(100);
            }
            if (!wifiScanEntered) {
                wifiScanEntered = true;
                startWiFiScan();
            }
            if (wifiScanState == 0 && !scanningPaused && now - lastScan > SCAN_INTERVAL_MS) {
                startWiFiScan();
                lastScan = now;
            }
            processWiFiScan();
            updateTargetTracking();
            if (now - targetPulseTimer > PULSE_INTERVAL_MS) {
                pulseSize = (pulseSize + 1) % 7;
                targetPulseTimer = now;
            }
            geigerSound();
            cleanupDevices();
            if (now - lastDrawTime > DRAW_INTERVAL_MS) {
                drawRadarBg();
                drawSweep();
                drawDevices();
                drawUI();
                lastDrawTime = now;
            }
            break;

        // ------------------------------------------------
        // CONFIG AP MODE (60s timer, then back to menu)
        // ------------------------------------------------
        case APP_CONFIG:
            if (!webUIActive) {
                Serial.println("[BOOT] Starting WiFi AP for config...");
                startWebUI();
                webUIActive = true;
                apStartTime = now;
                apClientEverConnected = false;
                tft.fillScreen(TFT_BLACK);
            }
            if (WiFi.softAPgetStationNum() > 0) {
                apClientEverConnected = true;
            }
            if (!apClientEverConnected && now - apStartTime > 60000) {
                Serial.println("[BOOT] AP timeout — returning to menu");
                stopWebUI();
                webUIActive = false;
                appState = APP_MENU;
                break;
            }
            if (webUIActive) {
                handleWebUI();
                if (!configDrawn) {
                    configDrawn = true;
                    tft.fillScreen(TFT_BLACK);
                    tft.setTextColor(TFT_GREEN);
                    tft.setTextSize(2);
                    tft.setCursor(15, 40);
                    tft.print("CONFIG AP");
                    tft.setTextSize(1);
                    tft.setCursor(15, 80);
                    tft.print("SSID: Tripwire-xxxx");
                    tft.setCursor(15, 100);
                    tft.print("IP: 192.168.4.1");
                    tft.setCursor(15, 130);
                    tft.print("Connect to configure");
                    tft.setCursor(15, 160);
                    tft.print("or hold to return");
                    tft.setTextColor(TFT_DARKGREEN);
                    tft.setCursor(15, 218);
                    tft.print("HOLD TO RETURN");
                }
            }
            break;

        // ------------------------------------------------
        // ABOUT SCREEN
        // ------------------------------------------------
        case APP_ABOUT:
            if (!aboutDrawn) {
                drawAbout();
                aboutDrawn = true;
            }
            break;

        // ------------------------------------------------
        // DEAUTH DETECTOR (easter egg from About screen)
        // ------------------------------------------------
        case APP_DEAUTH:
            if (!deauthDrawn) {
                deauthDrawn = true;
                deauthCount = 0;
                deauthDetected = false;
                deauthChannel = 1;
                deauthChanTimer = millis();
                strcpy(deauthLastSrc, "none");
                drawDeauthScreen();
                if (!deauthActive) {
                    WiFi.mode(WIFI_STA);
                    WiFi.disconnect();
                    delay(100);
                    esp_wifi_start();
                    wifi_promiscuous_filter_t filt = {WIFI_PROMIS_FILTER_MASK_MGMT};
                    esp_wifi_set_promiscuous_filter(&filt);
                    esp_wifi_set_promiscuous_rx_cb(deauthSnifferCallback);
                    esp_wifi_set_channel(deauthChannel, WIFI_SECOND_CHAN_NONE);
                    esp_wifi_set_promiscuous(true);
                    deauthActive = true;
                }
            }
            // Channel hop every 300ms
            if (millis() - deauthChanTimer > 300) {
                deauthChanTimer = millis();
                deauthChannel = (deauthChannel % 11) + 1;
                esp_wifi_set_channel(deauthChannel, WIFI_SECOND_CHAN_NONE);
            }
            if (deauthDetected) {
                deauthDetected = false;
                deauthFlashUntil = millis() + 100;
                tone(BUZZER_PIN, 4000, 10);
                // Update counter + last source
                tft.fillRect(100, 100, 60, 12, TFT_BLACK);
                tft.setTextColor(TFT_YELLOW);
                tft.setCursor(100, 100);
                tft.print(deauthCount);
                tft.fillRect(20, 145, 200, 12, TFT_BLACK);
                tft.setTextColor(TFT_ORANGE);
                tft.setCursor(20, 145);
                tft.print(deauthLastSrc);
            }
            if (millis() < deauthFlashUntil) {
                tft.fillRect(70, 60, 100, 6, TFT_RED);
            } else {
                tft.fillRect(70, 60, 100, 6, TFT_BLACK);
            }
            break;
    }

    yield();
}