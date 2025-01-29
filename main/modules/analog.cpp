#include "analog.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"

REGISTER_MODULE_DEFAULTS(Analog)

const std::map<std::string, Variable_ptr> Analog::get_defaults() {
    auto properties = Module::get_defaults();
    properties.insert({
        {"raw", std::make_shared<IntegerVariable>()},
        {"voltage", std::make_shared<NumberVariable>()},
    });
    return properties;
}

void Analog::set_error_descriptions() {
    error_descriptions = {
        {0x01, "Setup failed"},
        {0x02, "Read failed"},
    };
}

Analog::Analog(const std::string name, uint8_t unit, uint8_t channel, float attenuation_level)
    : Module(analog, name), unit(unit), channel(channel) {
    this->properties = Analog::get_defaults();

    if (unit < 1 || unit > 2) {
        echo("warning: invalid unit, using default 1");
        unit = 1;
    }

    const uint8_t max_channel = unit == 1 ? ADC_CHANNEL_7 : ADC_CHANNEL_9;
    if (channel > max_channel) {
        echo("warning: invalid channel, using default 0");
        channel = 0;
    }

    adc_atten_t attenuation;
    if (attenuation_level == 0.0) {
        attenuation = ADC_ATTEN_DB_0;
    } else if (attenuation_level == 2.5) {
        attenuation = ADC_ATTEN_DB_2_5;
    } else if (attenuation_level == 6.0) {
        attenuation = ADC_ATTEN_DB_6;
    } else if (attenuation_level == 11.0) {
        attenuation = ADC_ATTEN_DB_12; // 11 dB is not supported anymore
    } else if (attenuation_level == 12.0) {
        attenuation = ADC_ATTEN_DB_12;
    } else {
        echo("warning: invalid attenuation level, using default of 12 dB");
        attenuation = ADC_ATTEN_DB_12;
    }

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = static_cast<adc_unit_t>(unit - 1),
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = ESP_OK;
    err |= adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err |= adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(channel), &config);

    adc_cali_line_fitting_efuse_val_t cali_val;
    esp_err_t cali_check = adc_cali_scheme_line_fitting_check_efuse(&cali_val);
    if (cali_check != ESP_OK || cali_val == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
        echo("warning: eFuse calibration data not available, using default reference voltage.");
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = static_cast<adc_unit_t>(unit - 1),
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100,
    };
    err |= adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
    if (err != ESP_OK) {
        this->set_error(0x01);
        abort();
    }
}

void Analog::step() {
    int raw_value;
    esp_err_t err = ESP_OK;
    err |= adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(channel), &raw_value);

    int voltage;
    err |= adc_cali_raw_to_voltage(adc_cali_handle, raw_value, &voltage);
    if (err != ESP_OK) {
        this->set_error(0x02);
        return;
    }

    this->properties.at("raw")->integer_value = raw_value;
    this->properties.at("voltage")->number_value = voltage * 0.001;

    Module::step();
}
