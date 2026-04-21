#pragma once

#include <Arduino.h>
#include <esp_adc_cal.h>
#include "config.h"

class Battery {
public:
    void init();
    float readVoltage();
    bool isLow();

private:
    void configureAdc();
    esp_adc_cal_characteristics_t adcChars_;
    uint32_t vref_ = 1100;
    bool initialized_ = false;
};
