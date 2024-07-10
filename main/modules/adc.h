#pragma once

#include "esp_adc_cal.h"
#include "module.h"

class Adc;
using Adc_ptr = std::shared_ptr<Adc>;

class Adc : public Module {
private:
    uint8_t unit = 0;
    uint8_t channel = 0;
    esp_adc_cal_characteristics_t adc_chars;

public:
    Adc(const std::string name, uint8_t unit, uint8_t channel, float attenuation_level);
    void step() override;
};
