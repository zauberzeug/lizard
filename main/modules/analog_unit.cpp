#include "analog_unit.h"
#include "../utils/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

REGISTER_MODULE_DEFAULTS(AnalogUnit)

const std::map<std::string, Variable_ptr> AnalogUnit::get_defaults() {
    return {};
}

AnalogUnit::AnalogUnit(const std::string name, uint8_t unit)
    : Module(analog_unit, name) {
    if (unit < 1 || unit > 2) {
        echo("error: invalid unit, using default 1");
        unit = 1;
    }

    unit_id = static_cast<adc_unit_t>(unit - 1);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit_id,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));
}
