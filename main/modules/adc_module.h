#pragma once

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "module.h"
#include <memory>
#include <string>
#include <vector>

class Adc;
using Adc_ptr = std::shared_ptr<Adc>;

class Adc : public Module {
private:
    uint8_t adc_unit_ = 0;
    int8_t channel_ = -1;
    int8_t attenuation_level_ = -1;
    adc1_channel_t channel1_ = ADC1_CHANNEL_MAX;
    adc2_channel_t channel2_ = ADC2_CHANNEL_MAX;
    esp_adc_cal_characteristics_t adc_chars_;

    bool channel1_mapper(const int &channel, adc1_channel_t &channel1);
    bool channel2_mapper(const int &channel, adc2_channel_t &channel2);
    bool validate_attenuation_level(const int &attenuation_level);
    bool setup_adc(const int &adc_unit, const int &channel, const int &attenuation_level);
    int32_t read_adc(const int &adc_unit, const int &channel, const int &attenuation_level);
    int32_t read_adc_raw(const int &adc_unit, const int &channel, const int &attenuation_level);

public:
    Adc(const std::string name, uint8_t adc_unit, uint8_t channel, uint8_t attenuation_level);
    void step() override;
};
