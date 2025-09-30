#include "analog.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"

REGISTER_MODULE_DEFAULTS(Analog)

const std::map<std::string, Variable_ptr> Analog::get_defaults() {
    return {
        {"raw", std::make_shared<IntegerVariable>()},
        {"voltage", std::make_shared<NumberVariable>()},
    };
}

Analog::Analog(const std::string name, const AnalogUnit_ptr unit_ref, gpio_num_t pin, float attenuation_level)
    : Module(analog, name), pin(pin), unit_ref(unit_ref) {
    this->properties = Analog::get_defaults();

    if (!unit_ref) {
        throw std::runtime_error("Analog requires a valid AnalogUnit");
    }

    adc_handle = unit_ref->get_handle();

    adc_unit_t detected_unit;
    adc_channel_t detected_channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(pin, &detected_unit, &detected_channel));
    if (detected_unit != unit_ref->get_unit_id()) {
        throw std::runtime_error("Analog pin does not belong to the provided AnalogUnit");
    }
    channel = detected_channel;

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
        echo("error: invalid attenuation level, using default of 12 dB");
        attenuation = ADC_ATTEN_DB_12;
    }

    adc_oneshot_chan_cfg_t config = {
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(channel), &config));

#ifdef CONFIG_IDF_TARGET_ESP32S3
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit_ref->get_unit_id(),
        .chan = static_cast<adc_channel_t>(channel),
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
#else
    adc_cali_line_fitting_efuse_val_t cali_val;
    esp_err_t cali_check = adc_cali_scheme_line_fitting_check_efuse(&cali_val);
    if (cali_check != ESP_OK || cali_val == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
        echo("warning: eFuse calibration data not available, using default reference voltage.");
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit_ref->get_unit_id(),
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle));
#endif
}

void Analog::step() {
    int raw_value;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(channel), &raw_value));

    int voltage;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw_value, &voltage));

    this->properties.at("raw")->integer_value = raw_value;
    this->properties.at("voltage")->number_value = voltage * 0.001;

    Module::step();
}
