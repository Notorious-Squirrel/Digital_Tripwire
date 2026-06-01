#include <TFT_eSPI.h>
#include <SPI.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <SD.h>
#include <FS.h>
#include "boot.h"
#include "WebUI.h"
#include "squirrel_egg.h"
#include "oui.h"

// ======================================================
// CONSTANTS
// ======================================================

#define MAX_DEVICES        40
#define MAX_KNOWN_DEVICES  75
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
// CYD ESP32-2432S028 (ILI9341) Layout
// ======================================================

#define DISP_W 320
#define DISP_H 240
#define CTR_X  160
#define CTR_Y  120
#define RAD_R  100

// ======================================================
// WIFI AP CACHE (for WiFi Hunt mode)
// ======================================================

#define MAX_CACHED_APS 15

struct CachedAP {
    char bssid[18];
    char ssid[24];
    int8_t rssi;
    int8_t channel;
    unsigned long lastSeen;
};
CachedAP cachedAPs[MAX_CACHED_APS];
int cachedAPCount = 0;

// ======================================================
// DISPLAY
// ======================================================

TFT_eSPI tft = TFT_eSPI();

// ======================================================
// PINS — CYD ESP32-2432S028
// ======================================================

#define TOUCH_CS  33
#define TOUCH_IRQ 36
#define BUZZER_PIN 25
#define SD_CS      5

// TFT_eSPI pins configured in User_Setup.h (ILI9341_2_DRIVER, HSPI: MOSI=13 MISO=12 SCK=14, CS=15, DC=2, RST=-1, BL=21)

// ======================================================
// MODES
// ======================================================

enum ScanMode { BLE_MODE, WIFI_MODE };
ScanMode currentMode = BLE_MODE;

enum AppState { APP_MENU, APP_BLE_SCAN, APP_WIFI_SCAN, APP_CONFIG, APP_ABOUT, APP_DEAUTH, APP_TRIPWIRE, APP_HUNTER, APP_WIFI_HUNT };
AppState appState = APP_MENU;
int menuIndex = 0;
const char* menuItems[] = {"BLE Scan", "WiFi Scan", "Config AP", "Deauth", "Tripwire", "Hunt", "W-Hunt", "About"};
const int menuCount = 8;

// ======================================================
// BLE
// ======================================================

NimBLEScan* pBLEScan = nullptr;

// ======================================================
// SCAN RESULT QUEUE (producer: BLE callback / consumer: loop)
// ======================================================

#define RESULT_QUEUE_SIZE 12

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

extern "C" void deauthSnifferCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
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
// DIGITAL TAILS (persistent device tracking)
// ======================================================

#define TAILS_WINDOW 18
#define TAILS_STRONG_RSSI -55
#define TAILS_WATCH_MIN 10
#define TAILS_ALERT_MIN 12
#define TAILS_MAX_SHOW 8
#define TAILS_MAX_TRACKED 25
#define TAILS_SCAN_INTERVAL_MS 2000
#define TAILS_BEEP_COOLDOWN_MS 10000

struct TailSlot {
    char mac[18];
    uint32_t bitmask;
    int seenCount;
};
static TailSlot tailSlots[TAILS_MAX_TRACKED];
static int tailSlotCount = 0;
static unsigned long tailsLastScan = 0;
static bool tailsDrawn = false;
static unsigned long tailsBeepTimer = 0;

int tripwirePhase = 1;
int tripwireBaselineCount = 0;
int tripwireAlertCount = 0;
int tripwireStrongestRssi = -100;
char tripwireStrongestMac[18] = "";
char tripwireLastNewMac[18] = "";
char tripwireStrongestName[24] = "";

static int popcount(uint32_t x) {
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
}

static int tailsBarValue(int rssi) {
    if (rssi >= -35) return 10;
    if (rssi >= -40) return 9;
    if (rssi >= -45) return 8;
    if (rssi >= -50) return 7;
    if (rssi >= -55) return 6;
    if (rssi >= -60) return 5;
    if (rssi >= -65) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -80) return 2;
    return 1;
}

static int findTailSlot(const char* mac) {
    for (int i = 0; i < tailSlotCount; i++) {
        if (strcmp(tailSlots[i].mac, mac) == 0) return i;
    }
    return -1;
}

static void updateTails() {
    for (int i = 0; i < tailSlotCount; i++) {
        tailSlots[i].bitmask = (tailSlots[i].bitmask << 1) & 0x3FFFF;
        tailSlots[i].seenCount = popcount(tailSlots[i].bitmask);
    }
    for (int d = 0; d < deviceCount; d++) {
        const char* mac = devices[d].mac;
        int t = findTailSlot(mac);
        if (t < 0) {
            if (tailSlotCount >= TAILS_MAX_TRACKED) continue;
            t = tailSlotCount++;
            strncpy(tailSlots[t].mac, mac, 17);
            tailSlots[t].mac[17] = '\0';
            tailSlots[t].bitmask = 0;
        }
        tailSlots[t].bitmask |= 1;
        tailSlots[t].seenCount = popcount(tailSlots[t].bitmask);
    }
}

static int getTailRssi(const char* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].mac, mac) == 0) return devices[i].rssi;
    }
    return -100;
}

static const char* getTailName(const char* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].mac, mac) == 0) return devices[i].name;
    }
    return "";
}

static void drawTailsScreen() {
    tft.fillScreen(TFT_BLACK);

    // Find top tail
    int bestIdx = -1, bestSeen = -1, bestRssi = -100;
    for (int i = 0; i < tailSlotCount; i++) {
        int s = tailSlots[i].seenCount;
        if (s == 0) continue;
        int r = getTailRssi(tailSlots[i].mac);
        if (s > bestSeen || (s == bestSeen && r > bestRssi)) {
            bestSeen = s; bestRssi = r; bestIdx = i;
        }
    }

    // Title
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(120, 6);
    tft.print("TAILS");

    // Status + confidence
    const char* status = "SCANNING";
    unsigned int statusColor = TFT_WHITE;
    if (bestSeen >= TAILS_ALERT_MIN && bestRssi >= TAILS_STRONG_RSSI) {
        status = "ALERT"; statusColor = TFT_RED;
    } else if (bestSeen >= TAILS_WATCH_MIN) {
        status = "WATCH"; statusColor = TFT_YELLOW;
    }

    int score = 0;
    if (bestSeen >= TAILS_ALERT_MIN) score += 2;
    else if (bestSeen >= TAILS_WATCH_MIN) score += 1;
    if (bestRssi >= TAILS_STRONG_RSSI) score += 2;
    else if (bestRssi >= -65) score += 1;
    if (tailSlotCount > 10) score += 1;
    const char* conf = (score >= 5) ? "HIGH" : (score >= 3) ? "MED" : "LOW";

    tft.setTextSize(1);
    tft.setTextColor(statusColor);
    tft.setCursor(30, 28);
    tft.print(status);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(122, 28);
    tft.printf("CONF:%s", conf);

    // Stats line
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(30, 44);
    if (bestIdx >= 0) {
        tft.printf("Tail: %d/%d", bestSeen, TAILS_WINDOW);
        tft.setCursor(168, 44);
        tft.printf("Devs: %d", tailSlotCount);
    } else {
        tft.print("Building window...");
    }

    // RSSI bar + name for top tail
    if (bestIdx >= 0) {
        int bv = tailsBarValue(bestRssi);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(30, 60);
        tft.printf("RSSI: %d dBm", bestRssi);
        tft.setCursor(30, 74);
        unsigned int barColor;
        if (bestRssi >= -55) barColor = TFT_RED;
        else if (bestRssi >= -65) barColor = TFT_ORANGE;
        else if (bestRssi >= -75) barColor = TFT_YELLOW;
        else barColor = TFT_GREEN;
        tft.setTextColor(barColor);
        for (int b = 0; b < bv; b++) tft.print("#");
        for (int b = bv; b < 10; b++) tft.print("-");

        const char* nm = getTailName(tailSlots[bestIdx].mac);
        if (nm && nm[0]) {
            tft.setTextColor(TFT_GREEN);
            tft.setCursor(30, 90);
            char buf[13];
            strncpy(buf, nm, 12);
            buf[12] = '\0';
            tft.print(buf);
        }
    }

    // Separator
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(30, 106);
    tft.print("--- Top Tails ---");

    // Gather and sort top tails
    struct TailEntry { int idx; int seen; int rssi; char mac[18]; int bars; };
    TailEntry sorted[TAILS_MAX_SHOW];
    int sortedCount = 0;
    for (int i = 0; i < tailSlotCount && sortedCount < TAILS_MAX_SHOW; i++) {
        if (tailSlots[i].seenCount == 0) continue;
        int r = getTailRssi(tailSlots[i].mac);
        int pos = sortedCount;
        while (pos > 0 && (tailSlots[i].seenCount > sorted[pos-1].seen ||
               (tailSlots[i].seenCount == sorted[pos-1].seen && r > sorted[pos-1].rssi)))
            pos--;
        if (pos < TAILS_MAX_SHOW) {
            if (sortedCount < TAILS_MAX_SHOW) sortedCount++;
            for (int j = sortedCount - 1; j > pos; j--) sorted[j] = sorted[j-1];
            sorted[pos].idx = i;
            sorted[pos].seen = tailSlots[i].seenCount;
            sorted[pos].rssi = r;
            strncpy(sorted[pos].mac, tailSlots[i].mac, 17);
            sorted[pos].mac[17] = '\0';
            sorted[pos].bars = tailsBarValue(r);
        }
    }

    // Display sorted tails
    for (int i = 0; i < sortedCount && i < TAILS_MAX_SHOW; i++) {
        int y = 118 + i * 12;
        tft.setTextSize(1);

        // Flag
        const char* flag = "  ";
        if (sorted[i].seen >= TAILS_ALERT_MIN && sorted[i].rssi >= TAILS_STRONG_RSSI) flag = "!!";
        else if (sorted[i].seen >= TAILS_WATCH_MIN) flag = "! ";

        tft.setTextColor(TFT_YELLOW);
        tft.setCursor(8, y);
        tft.print(flag);

        // Short MAC
        tft.setTextColor(sorted[i].seen >= TAILS_WATCH_MIN ? TFT_WHITE : TFT_DARKGREEN);
        tft.setCursor(28, y);
        char sm[10];
        const char* m = sorted[i].mac;
        snprintf(sm, 10, "%c%c:%c%c:%c%c:%c%c", m[9], m[10], m[12], m[13], m[15], m[16], m[0], m[1]);
        tft.print(sm);

        // Seen count
        tft.setTextColor(TFT_CYAN);
        tft.setCursor(136, y);
        tft.printf("%2d/%d", sorted[i].seen, TAILS_WINDOW);

        // RSSI
        tft.setTextColor(sorted[i].rssi >= TAILS_STRONG_RSSI ? TFT_RED : TFT_WHITE);
        tft.setCursor(190, y);
        tft.print(sorted[i].rssi);

        // Bars
        tft.setTextColor(sorted[i].rssi >= -55 ? TFT_RED : TFT_GREEN);
        tft.setCursor(218, y);
        for (int b = 0; b < sorted[i].bars; b++) tft.print("#");
    }

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(95, 218);
    tft.print("HOLD TO RETURN");

    // Update web-accessible globals
    tripwireBaselineCount = tailSlotCount;
    tripwireStrongestRssi = bestRssi;
    if (bestIdx >= 0) {
        strncpy(tripwireLastNewMac, tailSlots[bestIdx].mac, 17);
        tripwireLastNewMac[17] = '\0';
        const char* nm = getTailName(tailSlots[bestIdx].mac);
        if (nm && nm[0]) {
            strncpy(tripwireStrongestName, nm, 23);
            tripwireStrongestName[23] = '\0';
        } else {
            tripwireStrongestName[0] = '\0';
        }
    }
}

// ======================================================
// DEVICE HUNTER (track a single target)
// ======================================================

#define HUNTER_PICK_COUNT 8

static bool hunterDrawn = false;
static bool hunterPicking = true;
static int hunterPickIndex = 0;
static bool hunterHunting = false;
static char hunterTargetMac[18] = "";
static char hunterTargetName[24] = "";
static unsigned long hunterBeepTimer = 0;
static int hunterTargetRssi = -100;
static int hunterPrevLevel = 0;

// ======================================================
// WIFI HUNT (track a single AP)
// ======================================================

#define WIFI_HUNT_PICK_COUNT 8

static bool wifiHuntDrawn = false;
static bool wifiHuntPicking = true;
static int wifiHuntPickIndex = 0;
static bool wifiHuntHunting = false;
static char wifiHuntTargetBssid[18] = "";
static char wifiHuntTargetSsid[24] = "";
static int wifiHuntTargetRssi = -100;
static int wifiHuntPrevLevel = 0;
static unsigned long wifiHuntBeepTimer = 0;
static unsigned long wifiHuntPacketTimer = 0;
static int8_t wifiHuntTargetChannel = 0;
static unsigned long wifiHuntRedrawTimer = 0;
static uint8_t wifiHuntTargetBytes[6];
static bool wifiHuntSniffing = false;

// Convert "XX:XX:XX:XX:XX:XX" string to 6 bytes
static void parseMacBytes(const char* mac, uint8_t* out) {
    for (int i = 0; i < 6; i++) {
        out[i] = strtol(mac + i * 3, NULL, 16);
    }
}

// Fast directed scan on a single channel for target BSSID
// Returns RSSI in dBm, or -100 if target not found
static int wifiHuntFastScan() {
    if (wifiHuntTargetChannel <= 0) return -100;
    WiFi.scanDelete();
    wifi_scan_config_t cfg = {};
    cfg.ssid = NULL;
    cfg.bssid = wifiHuntTargetBytes;
    cfg.channel = wifiHuntTargetChannel;
    cfg.show_hidden = true;
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min = 80;
    cfg.scan_time.active.max = 120;
    esp_wifi_scan_start(&cfg, true);
    unsigned long start = millis();
    while (millis() - start < 300) {
        uint16_t num = 0;
        esp_wifi_scan_get_ap_num(&num);
        if (num > 0) {
            wifi_ap_record_t records[1];
            uint16_t count = 1;
            esp_wifi_scan_get_ap_records(&count, records);
            if (count > 0) {
                esp_wifi_scan_stop();
                WiFi.scanDelete();
                return records[0].rssi;
            }
        }
        delay(5);
    }
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    return -100;
}

static void drawHunterPickScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(100, 10);
    tft.print("HUNT");

    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(15, 34);
    tft.print("Pick a target device:");

    // Gather top tails sorted by seen + RSSI
    struct { int idx; int seen; int rssi; char mac[18]; int bars; } list[HUNTER_PICK_COUNT];
    int lc = 0;
    for (int i = 0; i < tailSlotCount && lc < HUNTER_PICK_COUNT; i++) {
        if (tailSlots[i].seenCount == 0) continue;
        int r = getTailRssi(tailSlots[i].mac);
        int pos = lc;
        while (pos > 0 && (tailSlots[i].seenCount > list[pos-1].seen ||
               (tailSlots[i].seenCount == list[pos-1].seen && r > list[pos-1].rssi)))
            pos--;
        if (pos < HUNTER_PICK_COUNT) {
            if (lc < HUNTER_PICK_COUNT) lc++;
            for (int j = lc-1; j > pos; j--) list[j] = list[j-1];
            list[pos].idx = i;
            list[pos].seen = tailSlots[i].seenCount;
            list[pos].rssi = r;
            strncpy(list[pos].mac, tailSlots[i].mac, 17);
            list[pos].mac[17] = '\0';
            list[pos].bars = tailsBarValue(r);
        }
    }

    if (lc == 0) {
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(15, 80);
        tft.print("No devices tracked yet.");
        tft.setCursor(15, 100);
        tft.print("Run Tripwire first to");
        tft.setCursor(15, 120);
        tft.print("build a tail history.");
        tft.setTextColor(TFT_DARKGREEN);
        tft.setCursor(15, 218);
        tft.print("HOLD TO RETURN");
        return;
    }

    int visible = min(lc, 6);
    int startY = 52;
    int itemH = 22;
    int highlightStart = constrain(hunterPickIndex - 2, 0, max(0, lc - visible));

    for (int i = 0; i < visible; i++) {
        int idx = highlightStart + i;
        if (idx >= lc) break;
        int y = startY + i * itemH;
        bool sel = (idx == hunterPickIndex);
        if (sel) {
            tft.fillRoundRect(8, y-2, 304, itemH+2, 4, tft.color565(30, 60, 30));
        }
        const char* flag = (list[idx].seen >= TAILS_ALERT_MIN && list[idx].rssi >= TAILS_STRONG_RSSI) ? "!!" :
                           (list[idx].seen >= TAILS_WATCH_MIN) ? "!" : "  ";
        tft.setTextColor(sel ? TFT_GREEN : TFT_YELLOW);
        tft.setCursor(12, y);
        tft.print(flag);

        const char* m = list[idx].mac;
        char sm[10];
        snprintf(sm, 10, "%c%c:%c%c:%c%c:%c%c", m[9], m[10], m[12], m[13], m[15], m[16], m[0], m[1]);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(30, y);
        tft.print(sm);
        tft.setTextColor(TFT_CYAN);
        tft.setCursor(148, y);
        tft.printf("%2d/%d", list[idx].seen, TAILS_WINDOW);
        tft.setTextColor(list[idx].rssi >= TAILS_STRONG_RSSI ? TFT_RED : TFT_WHITE);
        tft.setCursor(196, y);
        tft.print(list[idx].rssi);
        tft.setTextColor(list[idx].rssi >= -55 ? TFT_RED : TFT_GREEN);
        tft.setCursor(220, y);
        for (int b = 0; b < list[idx].bars; b++) tft.print("#");
    }

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(55, 218);
    tft.print("HOLD TO RETURN");
}

static void drawHuntScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(100, 10);
    tft.print("HUNT");

    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(15, 34);
    tft.print("Target:");

    if (hunterTargetName[0]) {
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(115, 34);
        char buf[13];
        strncpy(buf, hunterTargetName, 12);
        buf[12] = '\0';
        tft.print(buf);
    }

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(15, 52);
    char sm[10];
    const char* m = hunterTargetMac;
    snprintf(sm, 10, "%c%c:%c%c:%c%c:%c%c", m[9], m[10], m[12], m[13], m[15], m[16], m[0], m[1]);
    tft.print(sm);

    // Signal level indicator
    int level = (hunterTargetRssi >= -35) ? 4 : (hunterTargetRssi >= -55) ? 3 :
                (hunterTargetRssi >= -75) ? 2 : 1;
    const char* levelLabel = "";
    unsigned int levelColor;
    switch (level) {
        case 4: levelLabel = "HOT"; levelColor = TFT_RED; break;
        case 3: levelLabel = "STRONG"; levelColor = TFT_ORANGE; break;
        case 2: levelLabel = "FAIR"; levelColor = TFT_YELLOW; break;
        default: levelLabel = "WEAK"; levelColor = TFT_GREEN; break;
    }

    tft.setTextSize(2);
    tft.setTextColor(levelColor);
    tft.setCursor(15, 78);
    tft.print(levelLabel);

    // RSSI value
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(160, 78);
    tft.printf("%d dBm", hunterTargetRssi);

    // Large RSSI bar
    int bv = tailsBarValue(hunterTargetRssi);
    tft.setTextSize(2);
    tft.setTextColor(levelColor);
    tft.setCursor(15, 110);
    for (int b = 0; b < bv; b++) tft.print("#");
    for (int b = bv; b < 10; b++) tft.print("-");

    // Animated signal-wave circles
    static unsigned long hunterWaveMs = 0;
    static int wavePhase = 0;
    if (millis() - hunterWaveMs > 400) {
        hunterWaveMs = millis();
        wavePhase = (wavePhase + 1) % 4;
    }
    unsigned long fadeColors[] = { TFT_WHITE, TFT_GREEN, TFT_YELLOW, TFT_RED };
    int cx = 260, cy = 105;
    for (int r = 0; r < 4; r++) {
        int radius = 10 + r * 8 + wavePhase * 2;
        tft.drawCircle(cx, cy, radius, fadeColors[r]);
    }

    // Beep history indicator
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(15, 140);
    tft.print("Signal beeps active");

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(15, 218);
    tft.print("HOLD TO RETURN");
}

// ======================================================
// WIFI HUNT SCREENS
// ======================================================

static void drawWifiHuntPickScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(80, 10);
    tft.print("W-HUNT");

    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(15, 34);
    tft.print("Pick a target AP:");

    if (cachedAPCount == 0) {
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(15, 80);
        tft.print("No APs cached.");
        tft.setCursor(15, 100);
        tft.print("Run WiFi Scan first");
        tft.setCursor(15, 120);
        tft.print("to populate AP list.");
        tft.setTextColor(TFT_DARKGREEN);
        tft.setCursor(15, 218);
        tft.print("HOLD TO RETURN");
        return;
    }

    int visible = min(cachedAPCount, 6);
    int startY = 52;
    int itemH = 22;
    int highlightStart = constrain(wifiHuntPickIndex - 2, 0, max(0, cachedAPCount - visible));

    for (int i = 0; i < visible; i++) {
        int idx = highlightStart + i;
        if (idx >= cachedAPCount) break;
        int y = startY + i * itemH;
        bool sel = (idx == wifiHuntPickIndex);
        if (sel) {
            tft.fillRoundRect(8, y-2, 304, itemH+2, 4, tft.color565(30, 60, 30));
        }

        // SSID (first 10 chars)
        tft.setTextColor(sel ? TFT_GREEN : TFT_WHITE);
        tft.setCursor(12, y);
        char buf[11];
        strncpy(buf, cachedAPs[idx].ssid, 10);
        buf[10] = '\0';
        tft.print(buf);

        // RSSI
        tft.setTextColor(cachedAPs[idx].rssi >= TAILS_STRONG_RSSI ? TFT_RED : TFT_WHITE);
        tft.setCursor(172, y);
        tft.print(cachedAPs[idx].rssi);
        tft.print("dBm");

        // Channel
        tft.setTextColor(TFT_DARKGREEN);
        tft.setCursor(232, y);
        tft.print("ch");
        tft.print(cachedAPs[idx].channel);
    }

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(55, 218);
    tft.print("HOLD TO SELECT");
}

static void drawWifiHuntScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(80, 10);
    tft.print("W-HUNT");

    tft.setTextSize(1);

    // Target SSID
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(15, 34);
    tft.print("Target:");
    if (wifiHuntTargetSsid[0]) {
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(115, 34);
        char buf[13];
        strncpy(buf, wifiHuntTargetSsid, 12);
        buf[12] = '\0';
        tft.print(buf);
    }

    // BSSID (short)
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(15, 52);
    const char* m = wifiHuntTargetBssid;
    char sm[10];
    snprintf(sm, 10, "%c%c:%c%c:%c%c:%c%c", m[9], m[10], m[12], m[13], m[15], m[16], m[0], m[1]);
    tft.print(sm);

    // Signal level
    int level = (wifiHuntTargetRssi >= -35) ? 4 : (wifiHuntTargetRssi >= -55) ? 3 :
                (wifiHuntTargetRssi >= -75) ? 2 : 1;
    const char* levelLabel = "";
    unsigned int levelColor;
    switch (level) {
        case 4: levelLabel = "HOT"; levelColor = TFT_RED; break;
        case 3: levelLabel = "STRONG"; levelColor = TFT_ORANGE; break;
        case 2: levelLabel = "FAIR"; levelColor = TFT_YELLOW; break;
        default: levelLabel = "WEAK"; levelColor = TFT_GREEN; break;
    }

    tft.setTextSize(2);
    tft.setTextColor(levelColor);
    tft.setCursor(15, 78);
    tft.print(levelLabel);

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(160, 78);
    tft.printf("%d dBm", wifiHuntTargetRssi);

    int bv = tailsBarValue(wifiHuntTargetRssi);
    tft.setTextSize(2);
    tft.setTextColor(levelColor);
    tft.setCursor(15, 110);
    for (int b = 0; b < bv; b++) tft.print("#");
    for (int b = bv; b < 10; b++) tft.print("-");

    // Animated wave
    static unsigned long wifiWaveMs = 0;
    static int wavePhase = 0;
    if (millis() - wifiWaveMs > 400) {
        wifiWaveMs = millis();
        wavePhase = (wavePhase + 1) % 4;
    }
    unsigned long fadeColors[] = { TFT_WHITE, TFT_GREEN, TFT_YELLOW, TFT_RED };
    int cx = 260, cy = 105;
    for (int r = 0; r < 4; r++) {
        int radius = 10 + r * 8 + wavePhase * 2;
        tft.drawCircle(cx, cy, radius, fadeColors[r]);
    }

    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(15, 140);
    tft.print("WiFi scanning...");

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(15, 218);
    tft.print("HOLD TO RETURN");
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
    tft.fillScreen(TFT_BLACK);
    const int cx = CTR_X, cy = CTR_Y;
    tft.drawCircle(cx, cy, 100, TFT_DARKGREEN);
    tft.drawCircle(cx, cy, 75, TFT_DARKGREEN);
    tft.drawCircle(cx, cy, 50, TFT_DARKGREEN);
    tft.drawCircle(cx, cy, 25, TFT_DARKGREEN);
    tft.drawLine(cx, 20, cx, 220, TFT_DARKGREEN);
    tft.drawLine(20, cy, DISP_W - 20, cy, TFT_DARKGREEN);
}

void drawSweep() {
    const int cx = CTR_X, cy = CTR_Y;
    float rad = sweepAngle * 0.0174533f;
    int x = cx + (int)(cosf(rad) * 95.0f);
    int y = cy + (int)(sinf(rad) * 95.0f);
    tft.drawLine(cx, cy, x, y, TFT_GREEN);
    sweepAngle += SWEEP_STEP;
    if (sweepAngle >= 360) sweepAngle = 0;
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
        int x = CTR_X + (int)(cosf(rad) * radius);
        int y = CTR_Y + (int)(sinf(rad) * radius);

        uint16_t colour = d.isNew ? TFT_CYAN : TFT_GREEN;
        if (d.rssi > RSSI_STRONG)   colour = TFT_YELLOW;
        if (d.rssi > RSSI_CRITICAL) colour = TFT_RED;
        if (i == activeTargetIndex) colour = TFT_MAGENTA;

        tft.fillCircle(x, y, 2, colour);
        tft.drawCircle(x, y, 4, colour);

        if (i == activeTargetIndex) {
            tft.drawCircle(x, y, 6 + pulseSize, TFT_MAGENTA);
            tft.drawCircle(x, y, 10 + pulseSize, TFT_MAGENTA);
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
    tft.setCursor(112, 15);
    tft.print(currentMode == BLE_MODE ? "BLE MODE" : "WIFI MODE");

    if (activeTargetIndex >= 0 && activeTargetIndex < deviceCount) {
        const DeviceInfo& t = devices[activeTargetIndex];

        tft.setTextSize(1);
        tft.setTextColor(TFT_MAGENTA);
        tft.setCursor(95, 38);
        tft.print("TARGET LOCK");

        tft.setCursor(85, 50);
        if (strcmp(t.name, "UNKNOWN") != 0) {
            char buf[13];
            strncpy(buf, t.name, 12);
            buf[12] = '\0';
            tft.print(buf);
        } else {
            tft.print("UNKNOWN DEVICE");
        }

        tft.setCursor(130, 62);
        tft.print(t.rssi);
        tft.print(" dBm");

        const char* vendor = lookupVendor(t.mac);
        if (vendor) {
            tft.setCursor(20, 74);
            tft.setTextColor(TFT_CYAN);
            tft.print(vendor);
        }
    }

    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(110, 180);
    tft.printf("LOGS %d", totalLogs);

    tft.setCursor(110, 200);
    tft.printf("LIVE %d", deviceCount);

    tft.setCursor(110, 220);
    tft.printf("KNOWN %d", knownDeviceCount);

    tft.setCursor(190, 220);
    tft.print(currentMode == BLE_MODE ? "WIFI>" : "BLE>");

    if (!sdReady) {
        tft.setTextColor(TFT_RED);
        tft.setCursor(5, 5);
        tft.setTextSize(1);
        tft.print("NO SD");
    }

    if (newDeviceDetected && millis() - alertTimer < 3000) {
        tft.fillRoundRect(85, 2, 150, 20, 6, TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(118, 5);
        tft.setTextSize(1);
        tft.print("NEW DEVICE");
    } else {
        newDeviceDetected = false;
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

        // Cache AP for WiFi Hunt mode
        if (cachedAPCount < MAX_CACHED_APS) {
            int ci = -1;
            for (int j = 0; j < cachedAPCount; j++) {
                if (strcmp(cachedAPs[j].bssid, mac) == 0) { ci = j; break; }
            }
            if (ci < 0) {
                ci = cachedAPCount;
                cachedAPCount++;
                strncpy(cachedAPs[ci].bssid, mac, 17);
                cachedAPs[ci].bssid[17] = '\0';
            }
            strncpy(cachedAPs[ci].ssid, name, 23);
            cachedAPs[ci].ssid[23] = '\0';
            cachedAPs[ci].rssi = rssi;
            cachedAPs[ci].channel = WiFi.channel(i);
            cachedAPs[ci].lastSeen = millis();
        }
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
// TOUCH HANDLER (XPT2046: IRQ pin LOW = pressed, HIGH = released)
// ======================================================

static bool touching = false;
static unsigned long touchStartMs = 0;

// XPT2046 raw read — returns true if a touch is detected
static bool readXPT2046() {
    if (digitalRead(TOUCH_IRQ) == HIGH) return false;  // IRQ active-low
    digitalWrite(TOUCH_CS, LOW);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(0x90);
    SPI.transfer16(0);
    SPI.transfer(0xD0);
    SPI.transfer16(0);
    SPI.endTransaction();
    digitalWrite(TOUCH_CS, HIGH);
    return true;  // IRQ was low, so a touch is present
}

void handleTouch() {
    if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
    bool pressed = readXPT2046();
    lastTouchMs = millis();

    if (pressed && !touching) {
        touching = true;
        touchStartMs = millis();
    } else if (!pressed && touching) {
        touching = false;
        unsigned long duration = millis() - touchStartMs;

        if (duration < 400) {
            // --- SHORT TAP ---
            if (millis() - lastEasterTapMs > 5000) { easterTapCount = 0; configTapCount = 0; }
            lastEasterTapMs = millis();
            tone(BUZZER_PIN, 3000, 15);
            if (appState == APP_CONFIG) {
                configTapCount++;
                if (configTapCount >= 7) {
                    configTapCount = 0;
                    tft.fillScreen(TFT_BLACK);
                    tft.setTextSize(3);
                    tft.setTextColor(TFT_RED);
                    tft.setCursor(65, 80);
                    tft.print("SQUIRREL!");
                    tft.setTextSize(2);
                    tft.setTextColor(TFT_YELLOW);
                    tft.setCursor(105, 120);
                    tft.print("***");
                    delay(5000);
                    return;
                }
                return;
            }
            if (appState == APP_HUNTER && hunterPicking) {
                int lc = 0;
                for (int i = 0; i < tailSlotCount; i++)
                    if (tailSlots[i].seenCount > 0) lc++;
                if (lc > 0) {
                    hunterPickIndex = (hunterPickIndex + 1) % lc;
                    drawHunterPickScreen();
                }
                return;
            }
            if (appState == APP_WIFI_HUNT && wifiHuntPicking) {
                if (cachedAPCount > 0) {
                    wifiHuntPickIndex = (wifiHuntPickIndex + 1) % cachedAPCount;
                    drawWifiHuntPickScreen();
                }
                return;
            }
            easterTapCount++;
            if (easterTapCount >= EASTER_EGG_TAPS) {
                easterTapCount = 0;
                drawEasterEgg();
                delay(5000);
                if (appState == APP_MENU) firstMenuDraw = true;
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
            // --- LONG HOLD ---
            easterTapCount = 0;
            configTapCount = 0;
            tone(BUZZER_PIN, 1500, 50);
            if (appState == APP_MENU) {
                switch (menuIndex) {
                    case 0: appState = APP_BLE_SCAN; currentMode = BLE_MODE; break;
                    case 1: appState = APP_WIFI_SCAN; currentMode = WIFI_MODE; break;
                    case 2: appState = APP_CONFIG; break;
                    case 3: appState = APP_DEAUTH; break;
                    case 4: appState = APP_TRIPWIRE; break;
                    case 5: appState = APP_HUNTER; break;
                    case 6: appState = APP_WIFI_HUNT; break;
                    case 7: appState = APP_ABOUT; break;
                }
            } else if (appState == APP_HUNTER && hunterPicking) {
                // Select target: find the one at current pick index
                int lc = 0;
                struct { int idx; } list[HUNTER_PICK_COUNT];
                for (int i = 0; i < tailSlotCount && lc < HUNTER_PICK_COUNT; i++) {
                    if (tailSlots[i].seenCount == 0) continue;
                    int r = getTailRssi(tailSlots[i].mac);
                    int pos = lc;
                    while (pos > 0 && (tailSlots[i].seenCount > tailSlots[list[pos-1].idx].seenCount ||
                           (tailSlots[i].seenCount == tailSlots[list[pos-1].idx].seenCount && r > getTailRssi(tailSlots[list[pos-1].idx].mac))))
                        pos--;
                    if (pos < HUNTER_PICK_COUNT) {
                        if (lc < HUNTER_PICK_COUNT) lc++;
                        for (int j = lc-1; j > pos; j--) list[j] = list[j-1];
                        list[pos].idx = i;
                    }
                }
                if (lc > 0 && hunterPickIndex < lc) {
                    int sel = list[hunterPickIndex].idx;
                    strncpy(hunterTargetMac, tailSlots[sel].mac, 17);
                    hunterTargetMac[17] = '\0';
                    const char* nm = getTailName(tailSlots[sel].mac);
                    if (nm && nm[0]) {
                        strncpy(hunterTargetName, nm, 23);
                        hunterTargetName[23] = '\0';
                    } else {
                        hunterTargetName[0] = '\0';
                    }
                    hunterHunting = true;
                    hunterPicking = false;
                    deviceCount = 0;
                    drawHuntScreen();
                }
            } else if (appState == APP_WIFI_HUNT && wifiHuntPicking) {
                if (cachedAPCount > 0 && wifiHuntPickIndex < cachedAPCount) {
                    strncpy(wifiHuntTargetBssid, cachedAPs[wifiHuntPickIndex].bssid, 17);
                    wifiHuntTargetBssid[17] = '\0';
                    strncpy(wifiHuntTargetSsid, cachedAPs[wifiHuntPickIndex].ssid, 23);
                    wifiHuntTargetSsid[23] = '\0';
                    wifiHuntTargetRssi = cachedAPs[wifiHuntPickIndex].rssi;
                    wifiHuntTargetChannel = cachedAPs[wifiHuntPickIndex].channel;
                    parseMacBytes(wifiHuntTargetBssid, wifiHuntTargetBytes);
                    wifiHuntHunting = true;
                    wifiHuntPicking = false;
                    wifiHuntSniffing = false;
                    wifiHuntPacketTimer = millis();
                    wifiHuntPrevLevel = 0;
                    deviceCount = 0;
                    drawWifiHuntScreen();
                }
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
    for (int y = 0; y < DISP_H; y += 4) {
        tft.fillRect(0, y, DISP_W, 2, tft.color565(y * 6 % 256, y * 3 % 256, 255 - y));
    }
    tft.fillCircle(CTR_X, 100, 50, TFT_BLACK);
    tft.fillCircle(CTR_X, 100, 48, tft.color565(255, 200, 0));
    tft.fillCircle(110, 90, 6, TFT_BLACK);
    tft.fillCircle(130, 90, 6, TFT_BLACK);
    tft.fillCircle(CTR_X, 105, 4, TFT_BLACK);
    tft.fillCircle(CTR_X, 110, 8, tft.color565(200, 0, 0));
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(95, 20);
    tft.print("EASTER EGG!");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(95, 180);
    tft.print("10 taps, nice!");
    tft.setCursor(85, 200);
    tft.print("now hold to return");
}

void drawMenu() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(105, 16);
    tft.print("TRIPWIRE");

    #define MENU_VISIBLE 4
    int start = constrain(menuIndex - 1, 0, max(0, menuCount - MENU_VISIBLE));

    tft.setTextSize(1);
    for (int i = 0; i < MENU_VISIBLE; i++) {
        int idx = start + i;
        if (idx >= menuCount) break;
        int y = 62 + i * 36;
        if (idx == menuIndex) {
            tft.fillRoundRect(90, y - 4, 140, 22, 6, TFT_DARKGREEN);
            tft.setTextColor(TFT_BLACK);
            tft.setCursor(102, y);
            tft.print(menuItems[idx]);
        } else {
            tft.setTextColor(TFT_GREEN);
            tft.setCursor(102, y);
            tft.print(menuItems[idx]);
        }
    }

    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(80, 218);
    tft.setTextSize(1);
    tft.print("TAP: NAV   HOLD: SELECT");
}

void drawAbout() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(85, 35);
    tft.print("TRIPWIRE");
    tft.setTextSize(1);
    tft.setCursor(65, 75);
    tft.print("BLE/WiFi Radar Scanner");
    tft.setCursor(65, 95);
    tft.print("CYD ESP32-2432S028");
    tft.setCursor(65, 125);
    tft.printf("Devices: %d", deviceCount);
    tft.setCursor(65, 145);
    tft.printf("Known: %d", knownDeviceCount);
    tft.setCursor(65, 165);
    tft.printf("Logs: %d", totalLogs);
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(65, 218);
    tft.print("HOLD TO RETURN");
}

// ======================================================
// DEAUTH DETECTOR SCREEN
// ======================================================

void drawDeauthScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(2);
    tft.setCursor(60, 30);
    tft.print("DEAUTH DETECTOR");
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(60, 75);
    tft.print("Monitoring WiFi...");
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(60, 100);
    tft.print("Deauths:");
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(140, 100);
    tft.print("0");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(60, 125);
    tft.print("Last source:");
    tft.setTextColor(TFT_ORANGE);
    tft.setCursor(60, 145);
    tft.print("none");
    tft.setTextColor(TFT_DARKGREEN);
    tft.setCursor(60, 218);
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

    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    pinMode(TOUCH_IRQ, INPUT_PULLUP);

    tft.init();
    tft.setRotation(1); // Landscape 320x240

    // Boot screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(70, 80);
    tft.print("TRIPWIRE X");
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(50, 120);
    tft.print("ESP32-CYD v2.0");
    delay(2000);

    // SD card
    sdReady = SD.begin(SD_CS);
    if (!sdReady) {
        Serial.println("[BOOT] SD FAILED");
    } else {
        Serial.println("[BOOT] SD OK");
        loadKnownDevices();
        int ouiCount = loadOuiFromSD();
        Serial.printf("[BOOT] OUI entries: %d\n", ouiCount);
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
            esp_wifi_stop();
            deauthActive = false;
        }
        if (lastAppState == APP_WIFI_HUNT && wifiHuntSniffing) {
            wifiHuntSniffing = false;
        }
        tailsDrawn = false;
        hunterDrawn = false;
        wifiHuntDrawn = false;
        bleScanBusy = false;
        wifiScanState = 0;
        bleScanEntered = false;
        wifiScanEntered = false;
        configDrawn = false;
        aboutDrawn = false;
        deauthDrawn = false;
        bleInitted = false;
        bleInitDone = false;
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
                drawDevices();
                drawUI();
                drawSweep();
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
                drawDevices();
                drawUI();
                drawSweep();
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
        // DEAUTH DETECTOR (menu item)
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
                    Serial.println("[DEAUTH] Starting sniffer...");
                    WiFi.disconnect(true);
                    delay(200);
                    esp_wifi_stop();
                    delay(100);
                    WiFi.mode(WIFI_STA);
                    delay(100);
                    esp_wifi_set_promiscuous_rx_cb(deauthSnifferCallback);
                    wifi_promiscuous_filter_t filt = {WIFI_PROMIS_FILTER_MASK_ALL};
                    esp_wifi_set_promiscuous_filter(&filt);
                    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
                    esp_wifi_set_promiscuous(true);
                    deauthActive = true;
                    Serial.println("[DEAUTH] Sniffer active");
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

        // ------------------------------------------------
        // DEVICE HUNTER (pick target → track live RSSI)
        // ------------------------------------------------
        case APP_HUNTER:
            if (!hunterDrawn) {
                if (pBLEScan == NULL) {
                    delay(10);
                    NimBLEDevice::init("");
                    pBLEScan = NimBLEDevice::getScan();
                    pBLEScan->setScanCallbacks(new MyScanCallbacks());
                    pBLEScan->setActiveScan(true);
                }
                hunterPicking = true;
                hunterHunting = false;
                hunterPickIndex = 0;
                hunterTargetMac[0] = '\0';
                hunterTargetName[0] = '\0';
                hunterTargetRssi = -100;
                hunterPrevLevel = 0;
                drawHunterPickScreen();
                hunterDrawn = true;
            }

            if (hunterPicking) {
                // Touch is handled in handleTouch() — just wait for selection
            }

            if (hunterHunting) {
                if (!bleScanBusy && (millis() - tailsLastScan >= 500)) {
                    tailsLastScan = millis();
                    startBLEScan();
                }
                processBLEScan();

                if (!bleScanBusy) {
                    // Find target in current scan results
                    bool found = false;
                    for (int i = 0; i < deviceCount; i++) {
                        if (strcmp(devices[i].mac, hunterTargetMac) == 0) {
                            hunterTargetRssi = devices[i].rssi;
                            if (devices[i].name[0]) {
                                strncpy(hunterTargetName, devices[i].name, 23);
                                hunterTargetName[23] = '\0';
                            }
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        hunterTargetRssi = min(hunterTargetRssi + 1, -90);
                        if (hunterTargetRssi > -90) hunterTargetRssi = -100;
                    }

                    // Signal-adaptive beep
                    int level = (hunterTargetRssi >= -35) ? 4 : (hunterTargetRssi >= -55) ? 3 :
                                (hunterTargetRssi >= -75) ? 2 : 1;
                    unsigned long beepInterval = 0;
                    switch (level) {
                        case 4: beepInterval = 200; break;
                        case 3: beepInterval = 400; break;
                        case 2: beepInterval = 800; break;
                        default: beepInterval = 1600; break;
                    }
                    if (level != hunterPrevLevel) {
                        hunterPrevLevel = level;
                        int freq = 1800 + level * 500;
                        int dur = 20 + level * 15;
                        tone(BUZZER_PIN, freq, dur);
                        hunterBeepTimer = millis();
                    } else if (millis() - hunterBeepTimer >= beepInterval) {
                        hunterBeepTimer = millis();
                        int freq = 1800 + level * 500;
                        int dur = 12 + level * 8;
                        tone(BUZZER_PIN, freq, dur);
                    }

                    // Update web globals for dashboard visibility
                    tripwireStrongestRssi = hunterTargetRssi;
                    if (hunterTargetName[0]) {
                        strncpy(tripwireStrongestName, hunterTargetName, 23);
                        tripwireStrongestName[23] = '\0';
                    }
                    strncpy(tripwireLastNewMac, hunterTargetMac, 17);
                    tripwireLastNewMac[17] = '\0';
                    tripwireBaselineCount = 1;

                    drawHuntScreen();
                }
            }
            break;

        // ------------------------------------------------
        // WIFI HUNT (pick AP → periodic scan for RSSI)
        // ------------------------------------------------
        case APP_WIFI_HUNT:
            if (!wifiHuntDrawn) {
                wifiHuntPicking = true;
                wifiHuntHunting = false;
                wifiHuntPickIndex = 0;
                wifiHuntTargetBssid[0] = '\0';
                wifiHuntTargetSsid[0] = '\0';
                wifiHuntTargetRssi = -100;
                wifiHuntPrevLevel = 0;
                wifiHuntSniffing = false;
                wifiHuntPacketTimer = millis();
                drawWifiHuntPickScreen();
                wifiHuntDrawn = true;
            }

            if (wifiHuntPicking) {
                // Touch handled in handleTouch()
            }

            if (wifiHuntHunting) {
                // Set STA mode and lock to target channel on first entry
                if (!wifiHuntSniffing && wifiHuntTargetChannel > 0) {
                    WiFi.disconnect(true);
                    delay(100);
                    esp_wifi_stop();
                    delay(100);
                    WiFi.mode(WIFI_STA);
                    delay(100);
                    esp_wifi_set_channel(wifiHuntTargetChannel, WIFI_SECOND_CHAN_NONE);
                    wifiHuntSniffing = true;
                    wifiHuntPacketTimer = millis();
                }

                // Every 250ms, do a fast directed scan for accurate RSSI
                if (millis() - wifiHuntPacketTimer >= 250) {
                    wifiHuntPacketTimer = millis();
                    int rssi = wifiHuntFastScan();
                    if (rssi > -100) {
                        wifiHuntTargetRssi = rssi;
                        Serial.printf("[W-HUNT] RSSI: %d\n", rssi);
                    }
                }

                // Decay after 2s of no data (AP disappeared)
                if (millis() - wifiHuntPacketTimer > 2000) {
                    wifiHuntTargetRssi = max(-100, wifiHuntTargetRssi - 1);
                }

                // Signal-adaptive beep
                int level = (wifiHuntTargetRssi >= -35) ? 4 : (wifiHuntTargetRssi >= -55) ? 3 :
                            (wifiHuntTargetRssi >= -75) ? 2 : 1;
                unsigned long beepInterval = 0;
                switch (level) {
                    case 4: beepInterval = 200; break;
                    case 3: beepInterval = 400; break;
                    case 2: beepInterval = 800; break;
                    default: beepInterval = 1600; break;
                }
                if (level != wifiHuntPrevLevel) {
                    wifiHuntPrevLevel = level;
                    int freq = 1800 + level * 500;
                    int dur = 20 + level * 15;
                    tone(BUZZER_PIN, freq, dur);
                    wifiHuntBeepTimer = millis();
                } else if (millis() - wifiHuntBeepTimer >= beepInterval) {
                    wifiHuntBeepTimer = millis();
                    int freq = 1800 + level * 500;
                    int dur = 12 + level * 8;
                    tone(BUZZER_PIN, freq, dur);
                }

                // Update web globals
                tripwireStrongestRssi = wifiHuntTargetRssi;
                if (wifiHuntTargetSsid[0]) {
                    strncpy(tripwireStrongestName, wifiHuntTargetSsid, 23);
                    tripwireStrongestName[23] = '\0';
                }
                strncpy(tripwireLastNewMac, wifiHuntTargetBssid, 17);
                tripwireLastNewMac[17] = '\0';
                tripwireBaselineCount = 1;

                // Redraw at 100ms for responsive updates
                if (millis() - wifiHuntRedrawTimer >= 100) {
                    wifiHuntRedrawTimer = millis();
                    drawWifiHuntScreen();
                }
            }
            break;

        // ------------------------------------------------
        // TRIPWIRE MODE (baseline → monitor)
        // ------------------------------------------------
        case APP_TRIPWIRE:
            if (!tailsDrawn) {
                if (pBLEScan == NULL) {
                    delay(10);
                    NimBLEDevice::init("");
                    pBLEScan = NimBLEDevice::getScan();
                    pBLEScan->setScanCallbacks(new MyScanCallbacks());
                    pBLEScan->setActiveScan(true);
                }
                tailsLastScan = 0;
                tailSlotCount = 0;
                memset(tailSlots, 0, sizeof(tailSlots));
                tripwireAlertCount = 0;
                tripwireStrongestRssi = -100;
                tripwireStrongestMac[0] = '\0';
                tripwireStrongestName[0] = '\0';
                tripwireLastNewMac[0] = '\0';
                deviceCount = 0;
                drawTailsScreen();
                tailsDrawn = true;
            }

            // Timer-gated scan cycle
            if (!bleScanBusy && (millis() - tailsLastScan >= TAILS_SCAN_INTERVAL_MS)) {
                tailsLastScan = millis();
                startBLEScan();
            }
            processBLEScan();

            // When scan completes, update tails
            if (!bleScanBusy) {
                updateTails();

                // Beep for high-confidence alerts (cooldown)
                if (millis() - tailsBeepTimer > TAILS_BEEP_COOLDOWN_MS) {
                    int bestSeen = 0, bestRssi = -100;
                    for (int i = 0; i < tailSlotCount; i++) {
                        int s = tailSlots[i].seenCount;
                        if (s == 0) continue;
                        int r = getTailRssi(tailSlots[i].mac);
                        if (s > bestSeen || (s == bestSeen && r > bestRssi)) {
                            bestSeen = s; bestRssi = r;
                        }
                    }
                    if (bestSeen >= TAILS_ALERT_MIN && bestRssi >= TAILS_STRONG_RSSI) {
                        int lvl = (bestRssi >= -35) ? 4 : (bestRssi >= -55) ? 3 : (bestRssi >= -65) ? 2 : 1;
                        tone(BUZZER_PIN, 2000 + lvl * 600, 40 + lvl * 20);
                        tailsBeepTimer = millis();
                    }
                }

                drawTailsScreen();
            }
            break;
    }

    yield();
}