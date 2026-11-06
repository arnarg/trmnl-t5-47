#include "config_store.h"

bool ConfigStore::begin() {
    return _prefs.begin(NVS_NAMESPACE, false);
}

void ConfigStore::end() {
    _prefs.end();
}

bool ConfigStore::hasWifiConfig() {
    return _prefs.isKey("wifi_ssid") && _prefs.getString("wifi_ssid").length() > 0;
}

String ConfigStore::getWifiSSID() {
    return _prefs.getString("wifi_ssid", "");
}

String ConfigStore::getWifiPass() {
    return _prefs.getString("wifi_pass", "");
}

String ConfigStore::getApiBase() {
    String base = _prefs.getString("api_base", DEFAULT_API_BASE);
    if (base.length() == 0) return DEFAULT_API_BASE;
    // Strip trailing slash
    while (base.endsWith("/")) base.remove(base.length() - 1);
    return base;
}

String ConfigStore::getApiKey() {
    return _prefs.getString("api_key", "");
}

String ConfigStore::getFriendlyId() {
    return _prefs.getString("friendly_id", "");
}

uint32_t ConfigStore::getRefreshRate() {
    return _prefs.getUInt("refresh_rate", DEFAULT_REFRESH_RATE);
}

bool ConfigStore::setWifiSSID(const String &val) {
    return _prefs.putString("wifi_ssid", val) > 0;
}

bool ConfigStore::setWifiPass(const String &val) {
    // putString returns 0 for empty strings but that's valid for open networks
    _prefs.putString("wifi_pass", val);
    return true;
}

bool ConfigStore::setApiBase(const String &val) {
    return _prefs.putString("api_base", val) > 0;
}

bool ConfigStore::setApiKey(const String &val) {
    return _prefs.putString("api_key", val) > 0;
}

bool ConfigStore::setFriendlyId(const String &val) {
    return _prefs.putString("friendly_id", val) > 0;
}

bool ConfigStore::setRefreshRate(uint32_t val) {
    return _prefs.putUInt("refresh_rate", val) > 0;
}

// The setters below only write when the value changes, since they run on
// most wakes and NVS writes cost flash wear and power.
String ConfigStore::getLastImage() {
    return _prefs.getString("last_image", "");
}

void ConfigStore::setLastImage(const String &val) {
    if (getLastImage() == val) return;
    if (val.length() == 0) _prefs.remove("last_image");
    else _prefs.putString("last_image", val);
}

uint8_t ConfigStore::getFailCount() {
    return _prefs.getUChar("fail_count", 0);
}

void ConfigStore::setFailCount(uint8_t val) {
    if (getFailCount() == val) return;
    _prefs.putUChar("fail_count", val);
}

void ConfigStore::clearAll() {
    _prefs.clear();
}

void ConfigStore::clearApiCredentials() {
    _prefs.remove("api_key");
    _prefs.remove("friendly_id");
}
