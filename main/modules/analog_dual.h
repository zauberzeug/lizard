#pragma once

#include "analog_unit.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "module.h"

class AnalogDual;
using AnalogDual_ptr = std::shared_ptr<AnalogDual>;

class AnalogDual : public Module {
private:
    AnalogUnit_ptr unit_ref;
    uint8_t channel1 = 0;
    uint8_t channel2 = 0;
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    adc_cali_handle_t adc_cali_handle_ch1 = nullptr;
    adc_cali_handle_t adc_cali_handle_ch2 = nullptr;

public:
    AnalogDual(const std::string name, const AnalogUnit_ptr unit_ref, uint8_t channel1, uint8_t channel2, float attenuation_level);
    void step() override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
