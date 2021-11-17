#include <chrono>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <string.h>
#include <vector>
#include "driver/uart.h"

#include "compilation/argument.h"
#include "compilation/await.h"
#include "compilation/method_call.h"
#include "compilation/routine.h"
#include "compilation/rule.h"
#include "modules/core.h"
#include "modules/module.h"
#include "utils/tictoc.h"
#include "global.h"
#include "storage.h"

#define BUFFER_SIZE 1024

extern "C"
{
#include "parser.h"
    void app_main();
}

std::string identifier_to_string(struct owl_ref ref)
{
    struct parsed_identifier identifier = parsed_identifier_get(ref);
    return std::string(identifier.identifier, identifier.length);
}

std::vector<Argument *> compile_arguments(struct owl_ref ref)
{
    std::vector<Argument *> arguments;
    for (struct owl_ref r = ref; !r.empty; r = owl_next(r))
    {
        struct parsed_argument argument = parsed_argument_get(r);
        if (!argument.integer.empty)
        {
            struct parsed_integer integer = parsed_integer_get(argument.integer);
            arguments.push_back(Argument::create_integer(integer.integer));
        }
        else if (!argument.neg_integer.empty)
        {
            struct parsed_neg_integer neg_integer = parsed_neg_integer_get(argument.neg_integer);
            struct parsed_integer integer = parsed_integer_get(neg_integer.integer);
            arguments.push_back(Argument::create_integer(-integer.integer));
        }
        else if (!argument.number.empty)
        {
            struct parsed_number number = parsed_number_get(argument.number);
            arguments.push_back(Argument::create_number(number.number));
        }
        else if (!argument.neg_number.empty)
        {
            struct parsed_neg_number neg_number = parsed_neg_number_get(argument.neg_number);
            struct parsed_number number = parsed_number_get(neg_number.number);
            arguments.push_back(Argument::create_number(-number.number));
        }
        else if (!argument.identifier.empty)
        {
            std::string identifier_string = identifier_to_string(argument.identifier);
            arguments.push_back(Argument::create_identifier(identifier_string));
        }
        else if (!argument.string.empty)
        {
            parsed_string string = parsed_string_get(argument.string);
            std::string string_string = std::string(string.string, string.length);
            arguments.push_back(Argument::create_string(string_string));
        }
        else
        {
            printf("error: invalid argument in range \"%d\"-\"%d\"\n", argument.range.start, argument.range.end);
        }
    }
    return arguments;
}

Expression *compile_expression(struct owl_ref ref)
{
    struct parsed_expression expression = parsed_expression_get(ref);
    if (!expression.integer.empty)
    {
        struct parsed_integer integer = parsed_integer_get(expression.integer);
        return new ConstExpression(integer.integer);
    }
    if (!expression.neg_integer.empty)
    {
        struct parsed_neg_integer neg_integer = parsed_neg_integer_get(expression.neg_integer);
        struct parsed_integer integer = parsed_integer_get(neg_integer.integer);
        return new ConstExpression(-integer.integer);
    }
    if (!expression.number.empty)
    {
        struct parsed_number number = parsed_number_get(expression.number);
        return new ConstExpression(number.number);
    }
    if (!expression.neg_number.empty)
    {
        struct parsed_neg_number neg_number = parsed_neg_number_get(expression.neg_number);
        struct parsed_number number = parsed_number_get(neg_number.number);
        return new ConstExpression(-number.number);
    }
    if (!expression.property_getter.empty)
    {
        struct parsed_property_getter property_getter = parsed_property_getter_get(expression.property_getter);
        struct parsed_module_name module_name = parsed_module_name_get(property_getter.module_name);
        std::string module_name_string = identifier_to_string(module_name.identifier);
        if (!Global::modules.count(module_name_string))
        {
            printf("error: unknown module \"%s\"\n", module_name_string.c_str());
            return nullptr;
        }
        struct parsed_property_name property_name = parsed_property_name_get(property_getter.property_name);
        std::string property_name_string = identifier_to_string(property_name.identifier);
        return new PropertyGetterExpression(Global::modules[module_name_string], property_name_string);
    }
    printf("error: invalid expression in range \"%d\"-\"%d\"\n", expression.range.start, expression.range.end);
    return nullptr;
}

Condition *compile_condition(struct owl_ref ref)
{
    struct parsed_condition condition = parsed_condition_get(ref);
    bool equality = parsed_comparison_get(condition.comparison).type == PARSED_EQUAL;
    return new Condition(compile_expression(condition.expression), compile_expression(owl_next(condition.expression)), equality);
}

std::vector<Action *> compile_actions(struct owl_ref ref)
{
    std::vector<Action *> actions;
    for (struct owl_ref r = ref; !r.empty; r = owl_next(r))
    {
        struct parsed_action action = parsed_action_get(r);
        if (!action.method_call.empty)
        {
            struct parsed_method_call method_call = parsed_method_call_get(action.method_call);
            struct parsed_module_name module_name = parsed_module_name_get(method_call.module_name);
            std::string module_name_string = identifier_to_string(module_name.identifier);
            if (!Global::modules.count(module_name_string))
            {
                printf("error: unknown module \"%s\"\n", module_name_string.c_str());
                break;
            }
            struct parsed_method method = parsed_method_get(method_call.method);
            std::string method_string = identifier_to_string(method.identifier);
            std::vector<Argument *> arguments = compile_arguments(method_call.argument);
            actions.push_back(new MethodCall(Global::modules[module_name_string], method_string, arguments));
        }
        else if (!action.await.empty)
        {
            struct parsed_await await = parsed_await_get(action.await);
            Condition *condition = compile_condition(await.condition);
            actions.push_back(new Await(condition));
        }
        else
        {
            printf("error: within routine defintions only method calls and awaits are implemented yet\n");
            break;
        }
    }
    return actions;
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
            std::string module_name_string = identifier_to_string(module_name.identifier);
            if (Global::modules.count(module_name_string))
            {
                printf("error: module \"%s\" already exists\n", module_name_string.c_str());
                return;
            }
            struct parsed_module_type module_type = parsed_module_type_get(constructor.module_type);
            std::string module_type_string = identifier_to_string(module_type.identifier);
            std::vector<Argument *> arguments = compile_arguments(constructor.argument);
            Module *module = Module::create(module_type_string, module_name_string, arguments);
            if (module == nullptr)
            {
                printf("error: could not create module\n");
                return;
            }
            Global::modules[module_name_string] = module;
        }
        else if (!statement.method_call.empty)
        {
            struct parsed_method_call method_call = parsed_method_call_get(statement.method_call);
            struct parsed_module_name module_name = parsed_module_name_get(method_call.module_name);
            std::string module_name_string = identifier_to_string(module_name.identifier);
            if (!Global::modules.count(module_name_string))
            {
                printf("error: unknown module \"%s\"\n", module_name_string.c_str());
                return;
            }
            struct parsed_method method = parsed_method_get(method_call.method);
            std::string method_string = identifier_to_string(method.identifier);
            std::vector<Argument *> arguments = compile_arguments(method_call.argument);
            Global::modules[module_name_string]->call_with_shadows(method_string, arguments);
        }
        else if (!statement.routine_call.empty)
        {
            struct parsed_routine_call routine_call = parsed_routine_call_get(statement.routine_call);
            struct parsed_routine_name routine_name = parsed_routine_name_get(routine_call.routine_name);
            std::string routine_name_string = identifier_to_string(routine_name.identifier);
            if (!Global::routines.count(routine_name_string))
            {
                printf("error: unknown routine \"%s\"\n", routine_name_string.c_str());
                return;
            }
            if (Global::routines[routine_name_string]->is_running())
            {
                printf("error: routine \"%s\" is alreaty running\n", routine_name_string.c_str());
            }
            else
            {
                Global::routines[routine_name_string]->start();
            }
        }
        else if (!statement.property_getter.empty)
        {
            struct parsed_property_getter property_getter = parsed_property_getter_get(statement.property_getter);
            struct parsed_module_name module_name = parsed_module_name_get(property_getter.module_name);
            std::string module_name_string = identifier_to_string(module_name.identifier);
            if (!Global::modules.count(module_name_string))
            {
                printf("error: unknown module \"%s\"\n", module_name_string.c_str());
                break;
            }
            struct parsed_property_name property_name = parsed_property_name_get(property_getter.property_name);
            std::string property_name_string = identifier_to_string(property_name.identifier);
            double value = Global::modules[module_name_string]->get(property_name_string);
            printf("%s.%s = %f\n", module_name_string.c_str(), property_name_string.c_str(), value);
            return;
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
            std::string routine_name_string = identifier_to_string(routine_name.identifier);
            if (Global::routines.count(routine_name_string))
            {
                printf("error: routine \"%s\" already exists\n", routine_name_string.c_str());
                return;
            }
            struct parsed_actions actions = parsed_actions_get(routine_definition.actions);
            Global::routines[routine_name_string] = new Routine(compile_actions(actions.action));
        }
        else if (!statement.rule_definition.empty)
        {
            struct parsed_rule_definition rule_definition = parsed_rule_definition_get(statement.rule_definition);
            struct parsed_actions actions = parsed_actions_get(rule_definition.actions);
            Global::rules.push_back(new Rule(compile_condition(rule_definition.condition), new Routine(compile_actions(actions.action))));
            return;
        }
        else
        {
            printf("error: unknown statement type\n");
            return;
        }
    }
}

void process_storage_command(const char *line)
{
    if (line[0] == '+')
    {
        Storage::append_to_startup(line + 1);
    }
    else if (line[0] == '-')
    {
        Storage::remove_from_startup(line + 1);
    }
    else if (line[0] == '?')
    {
        Storage::print_startup(line + 1);
    }
    else if (line[0] == '!')
    {
        Storage::save_startup();
    }
    else
    {
        printf("error: unrecognized storage command\n");
    }
}

void process_lizard(const char *line)
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
    Global::modules["core"] = new Core("core");

    Storage::init();
    process_lizard(Storage::startup.c_str());

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
    while (true)
    {
        int len = uart_read_bytes(UART_NUM_0, (uint8_t *)line, BUFFER_SIZE, 20 / portTICK_RATE_MS);
        line[len - 1] = 0;
        if (len > 2 && line[0] == '!')
        {
            process_storage_command(line + 1);
        }
        else if (len > 1)
        {
            process_lizard(line);
        }

        for (auto const &item : Global::modules)
        {
            item.second->step();
        }

        for (auto const &rule : Global::rules)
        {
            if (rule->condition->evaluate() && !rule->routine->is_running())
            {
                rule->routine->start();
            }
        }

        for (auto const &item : Global::routines)
        {
            item.second->step();
        }
    }
}
