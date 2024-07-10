#include "analog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"

Analog::Analog(const std::string name, uint8_t unit, uint8_t channel, float attenuation_level)
    : Module(analog, name), unit(unit) {
    if (unit < 1 || unit > 2) {
        echo("error: invalid unit, using default 1");
        unit = 1;
    }

    uint8_t max_channel = unit == 1 ? ADC1_CHANNEL_MAX : ADC2_CHANNEL_MAX;
    if (channel > max_channel) {
        echo("error: invalid channel, using default 0");
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
        attenuation = ADC_ATTEN_DB_11;
    } else {
        echo("error: invalid attenuation level, using default 0");
        attenuation = ADC_ATTEN_DB_0;
    }

    if (unit == 1) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(static_cast<adc1_channel_t>(channel), attenuation);
        esp_adc_cal_characterize(ADC_UNIT_1, attenuation, ADC_WIDTH_BIT_12, 1100, &this->characteristics);
    } else {
        adc2_config_channel_atten(static_cast<adc2_channel_t>(channel), attenuation);
        esp_adc_cal_characterize(ADC_UNIT_2, attenuation, ADC_WIDTH_BIT_12, 1100, &this->characteristics);
    }

    this->properties["raw"] = std::make_shared<IntegerVariable>();
    this->properties["voltage"] = std::make_shared<NumberVariable>();
}

void Analog::step() {
    int32_t reading = 0;
    if (this->unit == 1) {
        reading = adc1_get_raw(static_cast<adc1_channel_t>(this->channel));
    } else {
        adc2_get_raw(static_cast<adc2_channel_t>(this->channel), ADC_WIDTH_BIT_12, &reading);
    }

    this->properties.at("raw")->integer_value = reading;
    this->properties.at("voltage")->number_value = 0.001 * esp_adc_cal_raw_to_voltage(reading, &this->characteristics);

    Module::step();
}
