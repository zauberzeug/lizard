#pragma once

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "module.h"
#include <map>
#include <memory>
#include <string>

class Adc;
using Adc_ptr = std::shared_ptr<Adc>;

class Adc : public Module {
private:
    uint8_t adc_unit_ = 0;
    adc1_channel_t channel1_ = ADC1_CHANNEL_MAX;
    adc2_channel_t channel2_ = ADC2_CHANNEL_MAX;
    esp_adc_cal_characteristics_t adc_chars_;
    std::map<float, adc_atten_t> attenuation_mapping = {
        {0, ADC_ATTEN_DB_0},
        {2.5, ADC_ATTEN_DB_2_5},
        {6, ADC_ATTEN_DB_6},
        {11, ADC_ATTEN_DB_11}};

    bool channel1_mapper(const int &channel, adc1_channel_t &channel1);
    bool channel2_mapper(const int &channel, adc2_channel_t &channel2);
    bool validate_attenuation_level(const float &input_atten_level, adc_atten_t &atten_enum);
    bool setup_adc(const int &adc_unit, const int &channel, const float &input_atten_level);
    int32_t read_adc(const int &adc_unit);
    int32_t read_adc_raw(const int &adc_unit);

public:
    Adc(const std::string name, uint8_t adc_unit, uint8_t channel, float attenuation_level);
    void step() override;
};
