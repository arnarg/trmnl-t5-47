#pragma once

#define FIRMWARE_VERSION    "1.0.0"
#define DEVICE_NAME_PREFIX  "TRMNL"
#define DEVICE_MODEL        "lilygo_t5_47"

// NVS
#define NVS_NAMESPACE       "trmnl"

// WiFi
#define WIFI_CONNECT_TIMEOUT_MS  20000
#define WIFI_RETRY_COUNT         3
#define WIFI_RETRY_DELAY_MS      2000

// API
#define DEFAULT_API_BASE    "https://usetrmnl.com"
#define DEFAULT_REFRESH_RATE 1800
#define SETUP_REFRESH_RATE   60
#define ERROR_REFRESH_RATE   300

// Retry backoff for transient failures (WiFi, network, server 5xx, image download).
// The first QUIET_RETRY_COUNT failures leave the last good image on screen;
// after that an error screen is shown once. Sleep doubles each failure:
// RETRY_BASE_SEC, 2x, 4x, ... capped at RETRY_MAX_SEC.
#define QUIET_RETRY_COUNT    3
#define RETRY_BASE_SEC       60
#define RETRY_MAX_SEC        3600

// Display
#define DISPLAY_WIDTH       960
#define DISPLAY_HEIGHT      540

// Battery
#define BATTERY_ADC_PIN     36
#define BATTERY_LOW_VOLTAGE 3.3f

// Button
#define BUTTON_PIN          39
#define LONG_PRESS_MS       3000

// Captive Portal
#define PORTAL_TIMEOUT_MS   600000  // 10 minutes
