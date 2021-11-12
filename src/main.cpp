#include <stdio.h>
#include "stdint.h"
#include "driver/uart.h"

#define BUFFER_SIZE 1024

extern "C"
{
#include "parser.h"
    void app_main();
}
void app_main()
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .use_ref_tick = false,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, BUFFER_SIZE * 2, 0, 0, NULL, 0);

    char *line = (char *)malloc(BUFFER_SIZE);
    struct owl_tree *tree;
    while (true)
    {
        int len = uart_read_bytes(UART_NUM_0, (uint8_t *)line, BUFFER_SIZE, 20 / portTICK_RATE_MS);
        if (len <= 1)
            continue;

        line[len] = 0;
        tree = owl_tree_create_from_string(line);
        struct source_range range;
        owl_error error = owl_tree_get_error(tree, &range);
        if (error == ERROR_INVALID_FILE)
            fprintf(stderr, "error: invalid file");
        else if (error == ERROR_INVALID_OPTIONS)
            fprintf(stderr, "error: invalid options");
        else if (error == ERROR_INVALID_TOKEN)
            fprintf(stderr, "error: invalid token at range %zu %zu\n", range.start, range.end);
        else if (error == ERROR_UNEXPECTED_TOKEN)
            fprintf(stderr, "error: unexpected token at range %zu %zu\n", range.start, range.end);
        else if (error == ERROR_MORE_INPUT_NEEDED)
            fprintf(stderr, "error: more input needed at range %zu %zu\n", range.start, range.end);
        else
            owl_tree_print(tree);
        owl_tree_destroy(tree);
    }
}
