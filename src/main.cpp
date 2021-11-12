#include <stdio.h>
#include <stdint.h>
#include <string.h>
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
    for (struct owl_ref r = statements.statement; !r.empty; r = owl_next(r))
    {
        struct parsed_statement statement = parsed_statement_get(r);
        if (!statement.assignment.empty)
        {
            printf("error: assignments are not implemented yet\n");
            return;
        }
        else if (!statement.await.empty)
        {
            printf("error: awaits are not implemented yet\n");
            return;
        }
        else if (!statement.call.empty)
        {
            printf("error: calls are not implemented yet\n");
            return;
        }
        else if (!statement.constructor.empty)
        {
            struct parsed_constructor constructor = parsed_constructor_get(statement.constructor);
            struct parsed_module_type module_type = parsed_module_type_get(constructor.module_type);
            struct parsed_identifier identifier = parsed_identifier_get(module_type.identifier);
            if (strncmp(identifier.identifier, "Pin", identifier.length) == 0)
            {
                struct parsed_argument argument = parsed_argument_get(constructor.argument);
                struct parsed_expression expression = parsed_expression_get(argument.expression);
                if (!expression.number.empty)
                {
                    struct parsed_number number = parsed_number_get(expression.number);
                    int pin = number.number;
                    printf("Creating Pin(%d)...\n", pin);
                }
                else
                {
                    printf("error: expecting number argument for Pin constructor\n");
                    return;
                }
            }
            else
            {
                printf("error: unknown module type %s\n", identifier.identifier);
                return;
            }
        }
        else if (!statement.definition.empty)
        {
            printf("error: definitions are not implemented yet\n");
            return;
        }
        else if (!statement.rule.empty)
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
