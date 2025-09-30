#include "temperature_sensor.h"
#include "../utils/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

REGISTER_MODULE_DEFAULTS(TemperatureSensor)

const std::map<std::string, Variable_ptr> TemperatureSensor::get_defaults() {
    return {
        {"raw_temp", std::make_shared<IntegerVariable>()},
        {"raw_ref", std::make_shared<IntegerVariable>()},
        {"voltage_temp", std::make_shared<NumberVariable>()},
        {"voltage_ref", std::make_shared<NumberVariable>()},
    };
}

TemperatureSensor::TemperatureSensor(const std::string name, const AnalogUnit_ptr unit_ref, uint8_t channel_temp, uint8_t channel_ref, float attenuation_level)
    : Module(temperature_sensor, name), unit_ref(unit_ref), channel_temp(channel_temp), channel_ref(channel_ref) {
    this->properties = TemperatureSensor::get_defaults();

    if (!unit_ref) {
        throw std::runtime_error("TemperatureSensor requires a valid AnalogUnit");
    }

    adc_handle = unit_ref->get_handle();

    const uint8_t max_channel = unit_ref->get_unit_id() == ADC_UNIT_1 ? ADC_CHANNEL_7 : ADC_CHANNEL_9;
    if (channel_temp > max_channel) {
        echo("error: invalid temperature channel, using default 0");
        channel_temp = 0;
    }
    if (channel_ref > max_channel) {
        echo("error: invalid reference channel, using default 0");
        channel_ref = 0;
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

    // Reuse oneshot unit from AnalogUnit; only configure channels here

    adc_oneshot_chan_cfg_t config = {
        .atten = attenuation,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(channel_temp), &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, static_cast<adc_channel_t>(channel_ref), &config));

#ifdef CONFIG_IDF_TARGET_ESP32S3
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit_ref->get_unit_id(),
            .chan = static_cast<adc_channel_t>(channel_temp),
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_temp));
    }
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit_ref->get_unit_id(),
            .chan = static_cast<adc_channel_t>(channel_ref),
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_ref));
    }
#else
    // TemperatureSensor is only supported for ESP32S3 devices in this codebase.
#endif
}

void TemperatureSensor::step() {
    int raw_t;
    int raw_r;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(channel_temp), &raw_t));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, static_cast<adc_channel_t>(channel_ref), &raw_r));

    int mv_t;
    int mv_r;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_temp, raw_t, &mv_t));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_ref, raw_r, &mv_r));

    this->properties.at("raw_temp")->integer_value = raw_t;
    this->properties.at("raw_ref")->integer_value = raw_r;
    this->properties.at("voltage_temp")->number_value = mv_t * 0.001;
    this->properties.at("voltage_ref")->number_value = mv_r * 0.001;

    Module::step();
}
