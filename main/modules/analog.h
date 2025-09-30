#pragma once

#include "analog_unit.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "module.h"

class Analog;
using Analog_ptr = std::shared_ptr<Analog>;

class Analog : public Module {
private:
    uint8_t channel = 0;
    AnalogUnit_ptr unit_ref;
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t adc_cali_handle;

public:
    Analog(const std::string name, const AnalogUnit_ptr unit_ref, uint8_t channel, float attenuation_level);
    void step() override;
    static const std::map<std::string, Variable_ptr> get_defaults();
};
