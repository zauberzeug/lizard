#pragma once

#include "driver/adc.h"
#include "module.h"
#include <memory>
#include <string>
#include <vector>

class Adc;
using Adc_ptr = std::shared_ptr<Adc>;

typedef struct {
    Adc *adc_instance;
    int64_t adc_num;
    int64_t channel;
    int64_t attenuation_level;
} AdcTaskArgs;

class Adc : public Module {
private:
    adc1_channel_t channel1_;
    adc2_channel_t channel2_;
    int delay_;

    bool channel1_mapper(const int &channel, adc1_channel_t &channel1);
    bool channel2_mapper(const int &channel, adc2_channel_t &channel2);
    bool validate_attenuation_level(const int &attenuation_level);
    bool setup_adc(const int &adc_num, const int &channel, const int &attenuation_level);

public:
    Adc(const std::string name);
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    static void adc_task(void *pvParameter);
    static void adc_tast_raw(void *pvParameter);
    void read_adc(const int &adc_num, const int &channel, const int &attenuation_level);
    void read_adc_raw(const int &adc_num, const int &channel, const int &attenuation_level);
    void set_delay(const int &delay);
    void stop_adc();
};