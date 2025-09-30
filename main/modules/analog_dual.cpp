#include "analog_dual.h"
#include "../utils/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

REGISTER_MODULE_DEFAULTS(AnalogDual)

const std::map<std::string, Variable_ptr> AnalogDual::get_defaults() {
    return {
        {"raw1", std::make_shared<IntegerVariable>()},
        {"raw2", std::make_shared<IntegerVariable>()},
        {"voltage1", std::make_shared<NumberVariable>()},
        {"voltage2", std::make_shared<NumberVariable>()},
    };
}

AnalogDual::AnalogDual(const std::string name, uint8_t unit, uint8_t channel1, uint8_t channel2, float attenuation_level)
    : Module(analog_dual, name), unit(unit), channel1(channel1), channel2(channel2) {
    this->properties = AnalogDual::get_defaults();

    if (unit < 1 || unit > 2) {
        echo("error: invalid unit, using default 1");
        unit = 1;
    }

    const uint8_t max_channel = unit == 1 ? ADC_CHANNEL_7 : ADC_CHANNEL_9;
    if (channel1 > max_channel) {
        echo("error: invalid channel1, using default 0");
        channel1 = 0;
    }
    if (channel2 > max_channel) {
        echo("error: invalid channel2, using default 0");
        channel2 = 0;
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
        echo("error: invalid attenuation level, using default of 12 dB");
        attenuation = ADC_ATTEN_DB_12;
    }

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = static_cast<adc_unit_t>(unit - 1),
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(channel1), &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(channel2), &config));

#ifdef CONFIG_IDF_TARGET_ESP32S3
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = static_cast<adc_unit_t>(unit - 1),
            .chan = static_cast<adc_channel_t>(channel1),
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle_ch1));
    }
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = static_cast<adc_unit_t>(unit - 1),
            .chan = static_cast<adc_channel_t>(channel2),
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle_ch2));
    }
#else
    // AnalogDual is only supported for ESP32S3 devices in this codebase.
#endif
}

void AnalogDual::step() {
    int raw_value1;
    int raw_value2;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(channel1), &raw_value1));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(channel2), &raw_value2));

    int voltage1;
    int voltage2;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle_ch1, raw_value1, &voltage1));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle_ch2, raw_value2, &voltage2));

    this->properties.at("raw1")->integer_value = raw_value1;
    this->properties.at("raw2")->integer_value = raw_value2;
    this->properties.at("voltage1")->number_value = voltage1 * 0.001;
    this->properties.at("voltage2")->number_value = voltage2 * 0.001;

    Module::step();
}
