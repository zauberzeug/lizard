#include "adc_module.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart.h"

static xTaskHandle adc_task_handle = NULL;
static volatile bool stop_adc_task = false;

Adc::Adc(const std::string name)
    : Module(adc, name) {

    channel1_ = adc1_channel_t::ADC1_CHANNEL_MAX;
    channel2_ = adc2_channel_t::ADC2_CHANNEL_MAX;
    delay_ = 1000;
    echo("created adc module");
}

bool Adc::setup_adc(const int &adc_num, const int &channel, const int &attenuation_level) {
    if (adc_num == 1) {
        echo("Configuring ADC1...");
        if (!channel1_mapper(channel, channel1_)) {
            echo("Failed: Invalid channel for ADC1.");
            return false;
        }
        if (!validate_attenuation_level(attenuation_level)) {
            echo("Failed: Invalid attenuation level for ADC1.");
            return false;
        }
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(channel1_, static_cast<adc_atten_t>(attenuation_level));
        return true;
    } else if (adc_num == 2) {
        echo("Configuring ADC2...");
        if (!channel2_mapper(channel, channel2_)) {
            echo("Failed: Invalid channel for ADC2.");
            return false;
        }
        if (!validate_attenuation_level(attenuation_level)) {
            echo("Failed: Invalid attenuation level for ADC2.");
            return false;
        }
        adc2_config_channel_atten(channel2_, static_cast<adc_atten_t>(attenuation_level));
        return true;
    }

    echo("Failed: Invalid or unsupported ADC module number.");
    return false;
}

bool Adc::channel1_mapper(const int &channel, adc1_channel_t &channel1) {
    if (channel < adc1_channel_t::ADC1_CHANNEL_MAX) {
        channel1 = static_cast<adc1_channel_t>(channel);
        return true;
    }
    return false;
}

bool Adc::channel2_mapper(const int &channel, adc2_channel_t &channel2) {
    if (channel < adc2_channel_t::ADC2_CHANNEL_MAX) {
        channel2 = static_cast<adc2_channel_t>(channel);
        return true;
    }
    return false;
}

bool Adc::validate_attenuation_level(const int &attenuation_level) {
    return (attenuation_level == ADC_ATTEN_DB_0 || attenuation_level == ADC_ATTEN_DB_2_5 || attenuation_level == ADC_ATTEN_DB_6 || attenuation_level == ADC_ATTEN_DB_12);
}

void Adc::adc_task(void *pvParameter) {
    AdcTaskArgs *args = static_cast<AdcTaskArgs *>(pvParameter);
    Adc *adc_instance = args->adc_instance;

    adc_instance->read_adc(args->adc_num, args->channel, args->attenuation_level);
    delete args;
    vTaskDelete(NULL);
}

void Adc::adc_tast_raw(void *pvParameter) {
    AdcTaskArgs *args = static_cast<AdcTaskArgs *>(pvParameter);
    Adc *adc_instance = args->adc_instance;

    adc_instance->read_adc_raw(args->adc_num, args->channel, args->attenuation_level);
    delete args;
    vTaskDelete(NULL);
}

void Adc::stop_adc() {
    if (adc_task_handle != NULL) {
        stop_adc_task = true;

        while (eTaskGetState(adc_task_handle) != eDeleted) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        adc_task_handle = NULL;
        echo("ADC task stopped");
    }
}

void Adc::read_adc(const int &adc_num, const int &channel, const int &attenuation_level) {

    echo("reading ADC %d channel %d with attenuation level %d", adc_num, channel, attenuation_level);

    esp_adc_cal_characteristics_t adc_chars;
    if (adc_num == 1) {
        esp_adc_cal_characterize(ADC_UNIT_1, static_cast<adc_atten_t>(attenuation_level), ADC_WIDTH_BIT_12, 1100, &adc_chars);
    } else if (adc_num == 2) {
        esp_adc_cal_characterize(ADC_UNIT_2, static_cast<adc_atten_t>(attenuation_level), ADC_WIDTH_BIT_12, 1100, &adc_chars);
    }

    while (!stop_adc_task) {
        int32_t adc_reading = 0;
        int32_t voltage = 0;
        if (adc_num == 1) {
            adc_reading = adc1_get_raw(channel1_);
            voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
        } else if (adc_num == 2) {
            adc2_get_raw(channel2_, ADC_WIDTH_BIT_12, &adc_reading);
            voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
        }
        echo("Voltage: %dmV", voltage);
        vTaskDelay(pdMS_TO_TICKS(delay_));
    }
}

void Adc::read_adc_raw(const int &adc_num, const int &channel, const int &attenuation_level) {
    echo("reading ADC %d channel %d with attenuation level %d", adc_num, channel, attenuation_level);
    while (!stop_adc_task) {
        int32_t adc_reading = 0;
        if (adc_num == 1) {
            adc_reading = adc1_get_raw(channel1_);
        } else if (adc_num == 2) {
            adc2_get_raw(channel2_, ADC_WIDTH_BIT_12, &adc_reading);
        }

        echo("ADC reading: %d", adc_reading);
        vTaskDelay(pdMS_TO_TICKS(delay_));
    }
}

void Adc::set_delay(const int &delay) {
    delay_ = delay;
    echo("Set delay to %d", delay_);
}

void Adc::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "read") {
        expect(arguments, 3, integer, integer, integer);
        if (adc_task_handle == NULL) {
            auto *adc_task_args = new AdcTaskArgs{
                this,
                arguments[0]->evaluate_integer(),
                arguments[1]->evaluate_integer(),
                arguments[2]->evaluate_integer(),
            };
            if (!setup_adc(adc_task_args->adc_num, adc_task_args->channel, adc_task_args->attenuation_level)) {
                return;
            }
            stop_adc_task = false;
            xTaskCreate(adc_task, "adc_task", 2048, adc_task_args, 5, &adc_task_handle);

        } else {
            echo("ADC task already running");
        }
    } else if (method_name == "read_raw") {
        expect(arguments, 3, integer, integer, integer);
        if (adc_task_handle == NULL) {
            auto *adc_task_args = new AdcTaskArgs{
                this,
                arguments[0]->evaluate_integer(),
                arguments[1]->evaluate_integer(),
                arguments[2]->evaluate_integer(),
            };
            if (!setup_adc(adc_task_args->adc_num, adc_task_args->channel, adc_task_args->attenuation_level)) {
                return;
            }
            stop_adc_task = false;
            xTaskCreate(adc_tast_raw, "adc_tast_raw", 2048, adc_task_args, 5, &adc_task_handle);

        } else {
            echo("ADC task already running");
        }
    } else if (method_name == "set_delay") {
        expect(arguments, 1, integer);
        set_delay(arguments[0]->evaluate_integer());
    } else if (method_name == "stop") {
        stop_adc();
    } else {
        Module::call(method_name, arguments);
    }
}
