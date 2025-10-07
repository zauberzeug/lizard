#include "temperature_sensor.h"
#include "../utils/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>

REGISTER_MODULE_DEFAULTS(TemperatureSensor)

const std::map<std::string, Variable_ptr> TemperatureSensor::get_defaults() {
    return {
        {"raw_temp", std::make_shared<IntegerVariable>()},
        {"raw_ref", std::make_shared<IntegerVariable>()},
        {"voltage_temp", std::make_shared<NumberVariable>()},
        {"voltage_ref", std::make_shared<NumberVariable>()},
        {"temperature_c", std::make_shared<NumberVariable>()},
    };
}

TemperatureSensor::TemperatureSensor(const std::string name, const AnalogUnit_ptr unit, gpio_num_t temp_pin, gpio_num_t ref_pin, float attenuation_level)
    : Module(temperature_sensor, name), unit(unit), temp_pin(temp_pin), ref_pin(ref_pin) {
    this->properties = TemperatureSensor::get_defaults();

    if (!unit) {
        throw std::runtime_error("TemperatureSensor module requires a valid unit");
    }

    adc_unit_t detected_unit;
    adc_channel_t detected_channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(temp_pin, &detected_unit, &detected_channel));
    if (detected_unit != unit->get_adc_unit()) {
        throw std::runtime_error("TemperatureSensor temp pin does not belong to the provided unit");
    }
    this->channel_temp = detected_channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ref_pin, &detected_unit, &detected_channel));
    if (detected_unit != unit->get_adc_unit()) {
        throw std::runtime_error("TemperatureSensor ref pin does not belong to the provided unit");
    }
    this->channel_ref = detected_channel;

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
    ESP_ERROR_CHECK(adc_oneshot_config_channel(this->unit->get_adc_handle(), this->channel_temp, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(this->unit->get_adc_handle(), this->channel_ref, &config));

#ifdef CONFIG_IDF_TARGET_ESP32S3
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit->get_adc_unit(),
            .chan = this->channel_temp,
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &this->adc_cali_temp));
    }
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit->get_adc_unit(),
            .chan = this->channel_ref,
            .atten = attenuation,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &this->adc_cali_ref));
    }
#else
    throw std::runtime_error("TemperatureSensor is only supported for ESP32S3 devices");
#endif
}

void TemperatureSensor::step() {
    int raw_t;
    int raw_r;
    ESP_ERROR_CHECK(adc_oneshot_read(this->unit->get_adc_handle(), this->channel_temp, &raw_t));
    esp_rom_delay_us(80);
    ESP_ERROR_CHECK(adc_oneshot_read(this->unit->get_adc_handle(), this->channel_ref, &raw_r));

    int mv_t;
    int mv_r;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(this->adc_cali_temp, raw_t, &mv_t));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(this->adc_cali_ref, raw_r, &mv_r));

    this->properties.at("raw_temp")->integer_value = raw_t;
    this->properties.at("raw_ref")->integer_value = raw_r;
    const double v_temp = mv_t * 0.001;
    const double v_ref = mv_r * 0.001;
    this->properties.at("voltage_temp")->number_value = v_temp;
    this->properties.at("voltage_ref")->number_value = v_ref;

    // Compute temperature using reference divider (10k top, 3k bottom) and NTC divider (NTC top, 3k bottom)
    const double R_BOTTOM = 3000.0;   // 3k to GND
    const double R_REF_TOP = 10000.0; // 10k to 3V3 (reference divider top)
    const double R0 = 10000.0;        // 10k at 25°C
    const double BETA = 3380.0;       // Beta value (B25/85) for B57332V5103F360 (B25/100 -> 3455, B25/85 -> 3380)
    const double T0 = 273.15 + 25.0;  // 25°C in Kelvin

    double temperature_c = NAN;
    if (v_temp > 0.0 && v_ref > 0.0) {
        const double r_ntc = (R_REF_TOP + R_BOTTOM) * (v_ref / v_temp) - R_BOTTOM;
        if (r_ntc > 0.0) {
            const double inv_T = (1.0 / T0) + (std::log(r_ntc / R0) / BETA);
            const double temp_k = 1.0 / inv_T;
            temperature_c = temp_k - 273.15;
        }
    }
    this->properties.at("temperature_c")->number_value = temperature_c;

    Module::step();
}
