#pragma once
#include <Arduino.h>

enum ScanMode { BLE_MODE, WIFI_MODE };

enum AppState {
    APP_MENU, APP_BLE_SCAN, APP_WIFI_SCAN, APP_CONFIG,
    APP_ABOUT, APP_DEAUTH, APP_TRIPWIRE, APP_HUNTER, APP_WIFI_HUNT
};

struct DeviceInfo {
    char name[24];
    char mac[18];
    int8_t rssi;
    bool isNew;
    unsigned long firstSeen;
    unsigned long lastSeen;
};
