#pragma once

#include "analog_unit.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "module.h"

class TemperatureSensor;
using TemperatureSensor_ptr = std::shared_ptr<TemperatureSensor>;

class TemperatureSensor : public Module {
private:
    AnalogUnit_ptr unit_ref;
    uint8_t channel_temp = 0;
    uint8_t channel_ref = 0;
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    adc_cali_handle_t adc_cali_temp = nullptr;
    adc_cali_handle_t adc_cali_ref = nullptr;

public:
    TemperatureSensor(const std::string name, const AnalogUnit_ptr unit_ref, uint8_t channel_temp, uint8_t channel_ref, float attenuation_level);
    void step() override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
