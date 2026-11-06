#include "trmnl_api.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

TrmnlApi::TrmnlApi(const String &apiBase, const String &macAddress, const String &apiKey)
    : _apiBase(apiBase), _mac(macAddress), _apiKey(apiKey) {}

SetupResult TrmnlApi::setup() {
    SetupResult result = {};
    result.success = false;
    result.apiStatus = -1;

    WiFiClientSecure client;
    client.setInsecure(); // Skip CA verification for now

    HTTPClient http;
    http.setConnectTimeout(30000);
    http.setTimeout(60000);

    String url = _apiBase + "/api/setup";
    Serial.printf("[TRMNL] GET %s\n", url.c_str());

    if (!http.begin(client, url)) {
        result.httpCode = -1;
        result.message = "Failed to connect to server";
        Serial.println("[TRMNL] HTTP begin failed for /api/setup");
        return result;
    }

    http.addHeader("ID", _mac);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("FW-Version", FIRMWARE_VERSION);
    http.addHeader("Model", DEVICE_MODEL);

    result.httpCode = http.GET();
    Serial.printf("[TRMNL] /api/setup response: %d\n", result.httpCode);

    if (result.httpCode == 200) {
        String body = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);

        if (err) {
            result.message = "JSON parse error: " + String(err.c_str());
            http.end();
            return result;
        }

        // Like the official firmware, the JSON "status" is authoritative when
        // the HTTP code is 200 (BYOS servers such as scrn report errors this way).
        int status = doc["status"] | -1;
        result.apiStatus = status;
        if (status == 200) {
            result.apiKey = doc["api_key"] | "";
            result.friendlyId = doc["friendly_id"] | "";
            result.imageUrl = doc["image_url"] | "";
            result.message = doc["message"] | "";
            result.success = result.apiKey.length() > 0;
            if (!result.success) result.message = "Server returned no API key";
        } else if (status == 404) {
            result.message = "Device MAC not registered on server";
        } else {
            result.message = "Server returned status " + String(status);
        }
    } else if (result.httpCode == 404) {
        result.message = "Device MAC not registered on server";
    } else if (result.httpCode < 0) {
        result.message = "Connection failed: " + http.errorToString(result.httpCode);
    } else {
        result.message = "HTTP " + String(result.httpCode);
        String body = http.getString();
        if (body.length() > 0 && body.length() < 200) {
            result.message += ": " + body;
        }
    }

    http.end();
    return result;
}

DisplayResult TrmnlApi::fetchDisplay(uint32_t currentRefreshRate, float batteryVoltage, int rssi,
                                     bool imageCached) {
    DisplayResult result = {};
    result.success = false;
    result.refreshRate = currentRefreshRate;
    result.resetFirmware = false;
    result.updateFirmware = false;
    result.apiStatus = -1;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(30000);
    http.setTimeout(60000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    String url = _apiBase + "/api/display";
    Serial.printf("[TRMNL] GET %s\n", url.c_str());

    if (!http.begin(client, url)) {
        result.httpCode = -1;
        result.message = "Failed to connect to server";
        Serial.println("[TRMNL] HTTP begin failed for /api/display");
        return result;
    }

    http.addHeader("ID", _mac);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Access-Token", _apiKey);
    http.addHeader("Refresh-Rate", String(currentRefreshRate));
    http.addHeader("Battery-Voltage", String(batteryVoltage, 2));
    http.addHeader("FW-Version", FIRMWARE_VERSION);
    http.addHeader("Model", DEVICE_MODEL);
    http.addHeader("RSSI", String(rssi));
    http.addHeader("Width", String(DISPLAY_WIDTH));
    http.addHeader("Height", String(DISPLAY_HEIGHT));
    http.addHeader("Image-Cached", imageCached ? "true" : "false");

    result.httpCode = http.GET();
    Serial.printf("[TRMNL] /api/display response: %d\n", result.httpCode);

    if (result.httpCode == 200) {
        String body = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);

        if (err) {
            result.message = "JSON parse error: " + String(err.c_str());
            http.end();
            return result;
        }

        // JSON "status" handling mirrors the official firmware
        // (handleApiDisplayResponse in src/display_session.cpp):
        //   0      → normal image response
        //   202    → device not set up yet; show the waiting screen
        //   500    → reset credentials and re-run /api/setup
        //   other  → ignored: keep the current screen, sleep the normal interval
        int status = doc["status"] | 0;
        result.apiStatus = status;

        if (status == 0 || status == 200) {
            result.success = true;
            result.imageUrl = doc["image_url"] | "";
            result.filename = doc["filename"] | "";
            result.resetFirmware = doc["reset_firmware"] | false;
            result.updateFirmware = doc["update_firmware"] | false;

            uint32_t newRefresh = doc["refresh_rate"] | 0;
            if (newRefresh > 0) {
                result.refreshRate = newRefresh;
            }

            result.message = "";
        } else if (status == 202) {
            // Server answered fine; there's just nothing to show yet.
            // success with an empty imageUrl → "Waiting for Setup" in main.
            result.success = true;
            result.message = "No user attached to this device";
            result.refreshRate = doc["refresh_rate"] | ERROR_REFRESH_RATE;
        } else if (status == 500) {
            result.message = "Server requested credential reset (status 500)";
            result.resetFirmware = true;
        } else {
            result.message = "API status " + String(status);
            uint32_t newRefresh = doc["refresh_rate"] | 0;
            if (newRefresh > 0) result.refreshRate = newRefresh;
        }
    } else if (result.httpCode == 401 || result.httpCode == 403) {
        result.message = "API key invalid or expired";
        result.resetFirmware = true; // Signal main to clear credentials
    } else if (result.httpCode < 0) {
        result.message = "Connection failed: " + http.errorToString(result.httpCode);
    } else {
        result.message = "HTTP " + String(result.httpCode);
    }

    http.end();
    return result;
}
