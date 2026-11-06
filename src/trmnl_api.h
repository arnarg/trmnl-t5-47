#pragma once

#include <Arduino.h>

struct SetupResult {
    bool success;
    String apiKey;
    String friendlyId;
    String imageUrl;
    int httpCode;
    int apiStatus;   // "status" from the JSON body; -1 if no body was parsed
    String message;
};

struct DisplayResult {
    bool success;
    String imageUrl;
    String filename;
    uint32_t refreshRate;
    bool resetFirmware;
    bool updateFirmware;
    int httpCode;
    int apiStatus;   // "status" from the JSON body; -1 if no body was parsed
    String message;
};

class TrmnlApi {
public:
    TrmnlApi(const String &apiBase, const String &macAddress, const String &apiKey);

    SetupResult setup();
    // imageCached: whether the previous wake skipped the redraw because the
    // image was unchanged (sent as the Image-Cached header, like the official firmware)
    DisplayResult fetchDisplay(uint32_t currentRefreshRate, float batteryVoltage, int rssi,
                               bool imageCached);

private:
    String _apiBase;
    String _mac;
    String _apiKey;
};
