#include <chrono>
#include <map>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <string.h>
#include "driver/uart.h"

#include "compilation/routine.h"
#include "compilation/method_call.h"
#include "modules/button.h"
#include "modules/led.h"
#include "modules/module.h"

#define BUFFER_SIZE 1024

std::map<std::string, Module *> modules;
std::map<std::string, Routine *> routines;

extern "C"
{
#include "parser.h"
    void app_main();
}

std::chrono::_V2::system_clock::time_point t;
void tic()
{
    t = std::chrono::high_resolution_clock::now();
}
void toc(const char *msg)
{
    auto dt = std::chrono::high_resolution_clock::now() - t;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(dt);
    printf("%s took %.3f ms\n", msg, 0.001 * us.count());
}

std::string to_string(struct owl_ref ref)
{
    struct parsed_identifier identifier = parsed_identifier_get(ref);
    return std::string(identifier.identifier, identifier.length);
}

void process_tree(owl_tree *tree)
{
    struct parsed_statements statements = owl_tree_get_parsed_statements(tree);
    for (struct owl_ref r = statements.statement; !r.empty; r = owl_next(r))
    {
        struct parsed_statement statement = parsed_statement_get(r);
        if (!statement.constructor.empty)
        {
            struct parsed_constructor constructor = parsed_constructor_get(statement.constructor);
            struct parsed_module_name module_name = parsed_module_name_get(constructor.module_name);
            std::string module_name_string = to_string(module_name.identifier);
            if (modules.count(module_name_string))
            {
                printf("error: module \"%s\" already exists\n", module_name_string.c_str());
                return;
            }
            struct parsed_module_type module_type = parsed_module_type_get(constructor.module_type);
            std::string module_type_string = to_string(module_type.identifier);
            Module *module;
            if (module_type_string == "Led")
            {
                struct parsed_argument argument = parsed_argument_get(constructor.argument);
                if (!argument.number.empty)
                {
                    struct parsed_number number = parsed_number_get(argument.number);
                    module = new Led((gpio_num_t)number.number);
                }
                else
                {
                    printf("error: expecting number argument for Led constructor\n");
                    return;
                }
            }
            else if (module_type_string == "Button")
            {
                struct parsed_argument argument = parsed_argument_get(constructor.argument);
                if (!argument.number.empty)
                {
                    struct parsed_number number = parsed_number_get(argument.number);
                    module = new Button((gpio_num_t)number.number);
                }
                else
                {
                    printf("error: expecting number argument for Button constructor\n");
                    return;
                }
            }
            else
            {
                printf("error: unknown module type \"%s\"\n", module_type_string.c_str());
                return;
            }
            modules[module_name_string] = module;
            module->name = module_name_string;
        }
        else if (!statement.method_call.empty)
        {
            struct parsed_method_call method_call = parsed_method_call_get(statement.method_call);
            struct parsed_module_name module_name = parsed_module_name_get(method_call.module_name);
            std::string module_name_string = to_string(module_name.identifier);
            if (!modules.count(module_name_string))
            {
                printf("error: unknown module \"%s\"\n", module_name_string.c_str());
                return;
            }
            struct parsed_method method = parsed_method_get(method_call.method);
            std::string method_string = to_string(method.identifier);
            modules[module_name_string]->call(method_string);
        }
        else if (!statement.assignment.empty)
        {
            printf("error: assignments are not implemented yet\n");
            return;
        }
        else if (!statement.routine_definition.empty)
        {
            struct parsed_routine_definition routine_definition = parsed_routine_definition_get(statement.routine_definition);
            struct parsed_routine_name routine_name = parsed_routine_name_get(routine_definition.routine_name);
            std::string routine_name_string = to_string(routine_name.identifier);
            if (routines.count(routine_name_string))
            {
                printf("error: routine \"%s\" already exists\n", routine_name_string.c_str());
                return;
            }
            Routine *routine = new Routine();
            struct parsed_actions actions = parsed_actions_get(routine_definition.actions);
            for (struct owl_ref r = actions.action; !r.empty; r = owl_next(r))
            {
                struct parsed_action action = parsed_action_get(r);
                if (!action.method_call.empty)
                {
                    struct parsed_method_call method_call = parsed_method_call_get(action.method_call);
                    struct parsed_module_name module_name = parsed_module_name_get(method_call.module_name);
                    std::string module_name_string = to_string(module_name.identifier);
                    if (!modules.count(module_name_string))
                    {
                        printf("error: unknown module \"%s\"\n", module_name_string.c_str());
                        return;
                    }
                    struct parsed_method method = parsed_method_get(method_call.method);
                    std::string method_string = to_string(method.identifier);
                    routine->actions.push_back(new MethodCall(modules[module_name_string], method_string));
                }
                else
                {
                    printf("error: within routine defintions only method calls are implemented yet\n");
                    return;
                }
            }
            routines[routine_name_string] = routine;
        }
        else if (!statement.rule_definition.empty)
        {
            printf("error: rule definitions are not implemented yet\n");
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
    tic();
    struct owl_tree *tree = owl_tree_create_from_string(line);
    toc("Tree creation");
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
        tic();
        process_tree(tree);
        toc("Tree traversal");
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

    process_line("blue = Led(25); green = Led(26); button = Button(33); button.pullup; blue.on");
    process_line("all_on := blue.on; green.on; end");

    char *line = (char *)malloc(BUFFER_SIZE);
    while (true)
    {
        int len = uart_read_bytes(UART_NUM_0, (uint8_t *)line, BUFFER_SIZE, 20 / portTICK_RATE_MS);
        if (len > 1)
        {
            line[len] = 0;
            process_line(line);
        }

        for (auto const &item : modules)
        {
            item.second->step();
        }
    }
}
