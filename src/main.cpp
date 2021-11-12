#include <stdio.h>
#include "stdint.h"
#include "driver/uart.h"

#define BUFFER_SIZE 1024

extern "C"
{
#include "parser.h"
    void app_main();
}

void process_tree(owl_tree *tree)
{
    struct parsed_statements statements = owl_tree_get_parsed_statements(tree);
    for (auto s = statements.statement; !s.empty; s = owl_next(s))
    {
        struct parsed_statement item = parsed_statement_get(s);
        if (!item.assignment.empty)
        {
            printf("error: assignments are not implemented yet\n");
            return;
        }
        else if (!item.await.empty)
        {
            printf("error: awaits are not implemented yet\n");
            return;
        }
        else if (!item.call.empty)
        {
            printf("error: calls are not implemented yet\n");
            return;
        }
        else if (!item.constructor.empty)
        {
            printf("error: constructors are not implemented yet\n");
            return;
        }
        else if (!item.definition.empty)
        {
            printf("error: definitions are not implemented yet\n");
            return;
        }
        else if (!item.rule.empty)
        {
            printf("error: rules are not implemented yet\n");
            return;
        }
        else
        {
            printf("error: unknown statement type\n");
            return;
        }
    }
}

void process_line(const char *line)
{
    printf(">> %s\n", line);
    struct owl_tree *tree = owl_tree_create_from_string(line);
    struct source_range range;
    owl_error error = owl_tree_get_error(tree, &range);
    if (error == ERROR_INVALID_FILE)
        printf("error: invalid file");
    else if (error == ERROR_INVALID_OPTIONS)
        printf("error: invalid options");
    else if (error == ERROR_INVALID_TOKEN)
        printf("error: invalid token at range %zu %zu\n", range.start, range.end);
    else if (error == ERROR_UNEXPECTED_TOKEN)
        printf("error: unexpected token at range %zu %zu\n", range.start, range.end);
    else if (error == ERROR_MORE_INPUT_NEEDED)
        printf("error: more input needed at range %zu %zu\n", range.start, range.end);
    else
    {
        owl_tree_print(tree);
        process_tree(tree);
    }
    owl_tree_destroy(tree);
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

    process_line("pin1 = Pin(15); pin2 = Pin(32)");

    char *line = (char *)malloc(BUFFER_SIZE);
    while (true)
    {
        int len = uart_read_bytes(UART_NUM_0, (uint8_t *)line, BUFFER_SIZE, 20 / portTICK_RATE_MS);
        if (len > 1)
        {
            line[len] = 0;
            process_line(line);
        }
    }
}
