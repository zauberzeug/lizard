#pragma once

#include "analog_unit.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "module.h"

class Analog;
using Analog_ptr = std::shared_ptr<Analog>;

class Analog : public Module {
private:
    gpio_num_t pin;
    adc_channel_t channel;
    AnalogUnit_ptr unit;
    adc_cali_handle_t adc_cali_handle;

public:
    Analog(const std::string name, const AnalogUnit_ptr unit, gpio_num_t pin, float attenuation_level);
    void step() override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
