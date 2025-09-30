#pragma once

#include "analog_unit.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "module.h"

class TemperatureSensor;
using TemperatureSensor_ptr = std::shared_ptr<TemperatureSensor>;

class TemperatureSensor : public Module {
private:
    AnalogUnit_ptr unit_ref;
    gpio_num_t temp_pin = GPIO_NUM_NC;
    gpio_num_t ref_pin = GPIO_NUM_NC;
    adc_channel_t channel_temp = (adc_channel_t)0;
    adc_channel_t channel_ref = (adc_channel_t)0;
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    adc_cali_handle_t adc_cali_temp = nullptr;
    adc_cali_handle_t adc_cali_ref = nullptr;

public:
    TemperatureSensor(const std::string name, const AnalogUnit_ptr unit_ref, gpio_num_t temp_pin, gpio_num_t ref_pin, float attenuation_level);
    void step() override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
