#pragma once

#include "esp_adc/adc_oneshot.h"
#include "module.h"

class AnalogUnit;
using AnalogUnit_ptr = std::shared_ptr<AnalogUnit>;

class AnalogUnit : public Module {
private:
    adc_unit_t adc_unit;
    adc_oneshot_unit_handle_t adc_handle;

public:
    AnalogUnit(const std::string name, uint8_t unit_id);
    adc_oneshot_unit_handle_t get_adc_handle() const { return this->adc_handle; }
    adc_unit_t get_adc_unit() const { return this->adc_unit; }
    static const std::map<std::string, Variable_ptr> get_defaults();
};
