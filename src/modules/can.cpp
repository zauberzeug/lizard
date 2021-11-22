#include "can.h"

#include "driver/twai.h"

Can::Can(std::string name, gpio_num_t rx_pin, gpio_num_t tx_pin, long baud_rate) : Module(can, name)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(tx_pin, rx_pin, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config;
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    switch (baud_rate)
    {
    case 1000000:
        t_config = TWAI_TIMING_CONFIG_1MBITS();
        break;
    case 800000:
        t_config = TWAI_TIMING_CONFIG_800KBITS();
        break;
    case 500000:
        t_config = TWAI_TIMING_CONFIG_500KBITS();
        break;
    case 250000:
        t_config = TWAI_TIMING_CONFIG_250KBITS();
        break;
    case 125000:
        t_config = TWAI_TIMING_CONFIG_125KBITS();
        break;
    case 100000:
        t_config = TWAI_TIMING_CONFIG_100KBITS();
        break;
    case 50000:
        t_config = TWAI_TIMING_CONFIG_50KBITS();
        break;
    case 25000:
        t_config = TWAI_TIMING_CONFIG_25KBITS();
        break;
    default:
        throw std::runtime_error("invalid baud rate");
    }

    g_config.rx_queue_len = 20;
    g_config.tx_queue_len = 20;

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
}

void Can::step()
{
    twai_message_t message;
    while (twai_receive(&message, pdMS_TO_TICKS(0)) == ESP_OK)
    {
        if (this->subscribers.count(message.identifier))
        {
            this->subscribers[message.identifier]->handle_can_msg(
                message.identifier,
                message.data_length_code,
                message.data);
        }

        if (this->output)
        {
            printf("can %03x", message.identifier);
            if (!(message.flags & TWAI_MSG_FLAG_RTR))
            {
                for (int i = 0; i < message.data_length_code; ++i)
                {
                    printf(",%02x", message.data[i]);
                }
            }
            printf("\n");
        }
    }
}