#include "battery.h"
#include "epd_driver.h"

void Battery::init() {
    configureAdc();
    initialized_ = true;
}

void Battery::configureAdc() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    esp_adc_cal_value_t calType = esp_adc_cal_characterize(
        ADC_UNIT_2,
        ADC_ATTEN_DB_12,
        ADC_WIDTH_BIT_12,
        1100,
        &adcChars_);

    if (calType == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        vref_ = adcChars_.vref;
    }
    Serial.printf("[BATT] Calibration type: %d, Vref: %u mV\n", calType, vref_);
}

float Battery::readVoltage() {
    if (!initialized_) {
        configureAdc();
        initialized_ = true;
    }

    epd_poweron();
    delay(10);

    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogRead(BATTERY_ADC_PIN);
        delayMicroseconds(100);
    }

    epd_poweroff();

    float avgRaw = (float)sum / 16.0f;
    float voltage = (avgRaw / 4095.0f) * 2.0f * 3.3f * ((float)vref_ / 1000.0f);
    if (voltage >= 4.2f) voltage = 4.2f;

    Serial.printf("[BATT] Raw avg: %.1f, Voltage: %.2fV\n", avgRaw, voltage);

    return voltage;
}

bool Battery::isLow() {
    return readVoltage() < BATTERY_LOW_VOLTAGE;
}
