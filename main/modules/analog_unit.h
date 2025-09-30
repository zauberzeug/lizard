#pragma once

#include "esp_adc/adc_oneshot.h"
#include "module.h"

class AnalogUnit;
using AnalogUnit_ptr = std::shared_ptr<AnalogUnit>;

class AnalogUnit : public Module {
private:
    adc_unit_t unit_id;
    adc_oneshot_unit_handle_t adc_handle;

public:
    AnalogUnit(const std::string name, uint8_t unit);
    adc_oneshot_unit_handle_t get_handle() const { return adc_handle; }
    adc_unit_t get_unit_id() const { return unit_id; }
    static const std::map<std::string, Variable_ptr> get_defaults();
};
