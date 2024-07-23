#pragma once

#include "esp_adc_cal.h"
#include "module.h"

class Analog;
using Analog_ptr = std::shared_ptr<Analog>;

class Analog : public Module {
private:
    uint8_t unit = 0;
    uint8_t channel = 0;
    esp_adc_cal_characteristics_t characteristics;

public:
    Analog(const std::string name, uint8_t unit, uint8_t channel, float attenuation_level);
    void step() override;
};
