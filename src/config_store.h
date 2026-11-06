#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class ConfigStore {
public:
    bool begin();
    void end();

    bool hasWifiConfig();

    String getWifiSSID();
    String getWifiPass();
    String getApiBase();
    String getApiKey();
    String getFriendlyId();
    uint32_t getRefreshRate();

    bool setWifiSSID(const String &val);
    bool setWifiPass(const String &val);
    bool setApiBase(const String &val);
    bool setApiKey(const String &val);
    bool setFriendlyId(const String &val);
    bool setRefreshRate(uint32_t val);

    // Identifier (filename, or URL if no filename) of the /api/display image
    // currently on the panel; empty when something else is on screen.
    String getLastImage();
    void setLastImage(const String &val);

    // Consecutive transient failures (WiFi / network / server / image download)
    uint8_t getFailCount();
    void setFailCount(uint8_t val);

    void clearAll();
    void clearApiCredentials();

private:
    Preferences _prefs;
};
