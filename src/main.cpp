#include <chrono>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <string.h>
#include <vector>
#include "driver/uart.h"

#include "compilation/await.h"
#include "compilation/expression.h"
#include "compilation/method_call.h"
#include "compilation/routine.h"
#include "compilation/routine_call.h"
#include "compilation/rule.h"
#include "compilation/variable_assignment.h"
#include "modules/core.h"
#include "modules/module.h"
#include "utils/tictoc.h"
#include "global.h"
#include "storage.h"

#define BUFFER_SIZE 1024

Core *core_module;

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

// std::vector<Expression *> compile_arguments(struct owl_ref ref)
// {
//     std::vector<Expression *> arguments;
//     for (struct owl_ref r = ref; !r.empty; r = owl_next(r))
//     {
//         struct parsed_argument argument = parsed_argument_get(r);
//         if (!argument.expression.empty)
//         {
//             Expression *expression = compile_expression(argument.expression);
//             arguments.push_back(Expression::create_expression(expression));
//         }
//         else if (!argument.number.empty)
//         {
//             struct parsed_number number = parsed_number_get(argument.number);
//             arguments.push_back(Expression::create_number(number.number));
//         }
//         else if (!argument.neg_number.empty)
//         {
//             struct parsed_neg_number neg_number = parsed_neg_number_get(argument.neg_number);
//             struct parsed_number number = parsed_number_get(neg_number.number);
//             arguments.push_back(Expression::create_number(-number.number));
//         }
//         else if (!argument.identifier.empty)
//         {
//             std::string identifier_string = identifier_to_string(argument.identifier);
//             arguments.push_back(Expression::create_identifier(identifier_string));
//         }
//         else if (!argument.string.empty)
//         {
//             parsed_string string = parsed_string_get(argument.string);
//             std::string string_string = std::string(string.string, string.length);
//             arguments.push_back(Expression::create_string(string_string));
//         }
//         else
//         {
//             printf("error: invalid argument in range \"%d\"-\"%d\"\n", argument.range.start, argument.range.end);
//         }
//     }
//     return arguments;
// }

Expression *compile_expression(struct owl_ref ref)
{
    struct parsed_expression expression = parsed_expression_get(ref);
    switch (expression.type)
    {
    case PARSED_INTEGER:
        return new IntegerExpression(parsed_integer_get(expression.integer).integer);
    case PARSED_STRING:
    {
        struct parsed_string string = parsed_string_get(expression.string);
        return new StringExpression(std::string(string.string, string.length));
    }
    default:
        printf("error: invalid expression in range \"%d\"-\"%d\"\n", expression.range.start, expression.range.end);
        return nullptr;
    }
    //     if (!expression.property_getter.empty)
    //     {
    //         struct parsed_property_getter property_getter = parsed_property_getter_get(expression.property_getter);
    //         struct parsed_module_name module_name = parsed_module_name_get(property_getter.module_name);
    //         std::string module_name_string = identifier_to_string(module_name.identifier);
    //         if (!Global::modules.count(module_name_string))
    //         {
    //             printf("error: unknown module \"%s\"\n", module_name_string.c_str());
    //             return nullptr;
    //         }
    //         struct parsed_property_name property_name = parsed_property_name_get(property_getter.property_name);
    //         std::string property_name_string = identifier_to_string(property_name.identifier);
    //         return new PropertyGetterExpression(Global::modules[module_name_string], property_name_string);
    //     }
    //     if (!expression.variable_name.empty)
    //     {
    //         struct parsed_variable_name variable_name = parsed_variable_name_get(expression.variable_name);
    //         std::string variable_name_string = identifier_to_string(variable_name.identifier);
    //         if (!Global::variables.count(variable_name_string))
    //         {
    //             printf("error: unknown variable \"%s\"\n", variable_name_string.c_str());
    //             return nullptr;
    //         }
    //         return new VariableGetterExpression(Global::variables[variable_name_string]);
    //     }
}

// std::vector<Action *> compile_actions(struct owl_ref ref)
// {
//     std::vector<Action *> actions;
//     for (struct owl_ref r = ref; !r.empty; r = owl_next(r))
//     {
//         struct parsed_action action = parsed_action_get(r);
//         if (!action.method_call.empty)
//         {
//             struct parsed_method_call method_call = parsed_method_call_get(action.method_call);
//             struct parsed_module_name module_name = parsed_module_name_get(method_call.module_name);
//             std::string module_name_string = identifier_to_string(module_name.identifier);
//             if (!Global::modules.count(module_name_string))
//             {
//                 printf("error: unknown module \"%s\"\n", module_name_string.c_str());
//                 break;
//             }
//             struct parsed_method_name method_name = parsed_method_name_get(method_call.method_name);
//             std::string method_name_string = identifier_to_string(method_name.identifier);
//             std::vector<Argument *> arguments = compile_arguments(method_call.argument);
//             actions.push_back(new MethodCall(Global::modules[module_name_string], method_name_string, arguments));
//         }
//         else if (!action.routine_call.empty)
//         {
//             struct parsed_routine_call routine_call = parsed_routine_call_get(action.routine_call);
//             struct parsed_routine_name routine_name = parsed_routine_name_get(routine_call.routine_name);
//             std::string routine_name_string = identifier_to_string(routine_name.identifier);
//             if (!Global::routines.count(routine_name_string))
//             {
//                 printf("error: unknown routine \"%s\"\n", routine_name_string.c_str());
//                 break;
//             }
//             actions.push_back(new RoutineCall(Global::routines[routine_name_string]));
//         }
//         else if (!action.variable_assignment.empty)
//         {
//             struct parsed_variable_assignment variable_assignment = parsed_variable_assignment_get(action.variable_assignment);
//             struct parsed_variable_name variable_name = parsed_variable_name_get(variable_assignment.variable_name);
//             std::string variable_name_string = identifier_to_string(variable_name.identifier);
//             if (!Global::variables.count(variable_name_string))
//             {
//                 printf("error: unknown variable \"%s\"\n", variable_name_string.c_str());
//                 break;
//             }
//             Variable *variable = Global::variables[variable_name_string];
//             Expression *expression = compile_expression(variable_assignment.expression);
//             actions.push_back(new VariableAssignment(variable, expression));
//         }
//         else if (!action.await.empty)
//         {
//             struct parsed_await await = parsed_await_get(action.await);
//             Condition *condition = compile_condition(await.condition);
//             actions.push_back(new Await(condition));
//         }
//         else
//         {
//             printf("error: unknown action type\n");
//             break;
//         }
//     }
//     return actions;
// }

void process_tree(owl_tree *tree)
{
    struct parsed_statements statements = owl_tree_get_parsed_statements(tree);
    for (struct owl_ref r = statements.statement; !r.empty; r = owl_next(r))
    {
        struct parsed_statement statement = parsed_statement_get(r);
        if (!statement.expression.empty)
        {
            Expression *expression = compile_expression(statement.expression);
            switch (expression->type)
            {
            case boolean:
                printf("%s\n", expression->evaluate_boolean() ? "true" : "false");
                break;
            case integer:
                printf("%d\n", expression->evaluate_integer());
                break;
            case number:
                printf("%f\n", expression->evaluate_number());
                break;
            case identifier:
                printf("%s\n", expression->evaluate_identifier().c_str());
                break;
            case string:
                printf("\"%s\"\n", expression->evaluate_string().c_str());
                break;
            }
        }
        // else if (!statement.constructor.empty)
        // {
        //     struct parsed_constructor constructor = parsed_constructor_get(statement.constructor);
        //     struct parsed_module_name module_name = parsed_module_name_get(constructor.module_name);
        //     std::string module_name_string = identifier_to_string(module_name.identifier);
        //     if (Global::modules.count(module_name_string))
        //     {
        //         printf("error: module \"%s\" already exists\n", module_name_string.c_str());
        //         return;
        //     }
        //     struct parsed_module_type module_type = parsed_module_type_get(constructor.module_type);
        //     std::string module_type_string = identifier_to_string(module_type.identifier);
        //     std::vector<Argument *> arguments = compile_arguments(constructor.argument);
        //     Module *module = Module::create(module_type_string, module_name_string, arguments);
        //     if (module == nullptr)
        //     {
        //         printf("error: could not create module\n");
        //         return;
        //     }
        //     Global::modules[module_name_string] = module;
        // }
        // else if (!statement.method_call.empty)
        // {
        //     struct parsed_method_call method_call = parsed_method_call_get(statement.method_call);
        //     struct parsed_module_name module_name = parsed_module_name_get(method_call.module_name);
        //     std::string module_name_string = identifier_to_string(module_name.identifier);
        //     if (!Global::modules.count(module_name_string))
        //     {
        //         printf("error: unknown module \"%s\"\n", module_name_string.c_str());
        //         return;
        //     }
        //     struct parsed_method_name method_name = parsed_method_name_get(method_call.method_name);
        //     std::string method_name_string = identifier_to_string(method_name.identifier);
        //     std::vector<Argument *> arguments = compile_arguments(method_call.argument);
        //     Global::modules[module_name_string]->call_with_shadows(method_name_string, arguments);
        // }
        // else if (!statement.routine_call.empty)
        // {
        //     struct parsed_routine_call routine_call = parsed_routine_call_get(statement.routine_call);
        //     struct parsed_routine_name routine_name = parsed_routine_name_get(routine_call.routine_name);
        //     std::string routine_name_string = identifier_to_string(routine_name.identifier);
        //     if (!Global::routines.count(routine_name_string))
        //     {
        //         printf("error: unknown routine \"%s\"\n", routine_name_string.c_str());
        //         return;
        //     }
        //     if (Global::routines[routine_name_string]->is_running())
        //     {
        //         printf("error: routine \"%s\" is alreaty running\n", routine_name_string.c_str());
        //     }
        //     else
        //     {
        //         Global::routines[routine_name_string]->start();
        //     }
        // }
        // else if (!statement.property_getter.empty)
        // {
        //     struct parsed_property_getter property_getter = parsed_property_getter_get(statement.property_getter);
        //     struct parsed_module_name module_name = parsed_module_name_get(property_getter.module_name);
        //     std::string module_name_string = identifier_to_string(module_name.identifier);
        //     if (!Global::modules.count(module_name_string))
        //     {
        //         printf("error: unknown module \"%s\"\n", module_name_string.c_str());
        //         return;
        //     }
        //     struct parsed_property_name property_name = parsed_property_name_get(property_getter.property_name);
        //     std::string property_name_string = identifier_to_string(property_name.identifier);
        //     double value = Global::modules[module_name_string]->get(property_name_string);
        //     printf("%s.%s = %f\n", module_name_string.c_str(), property_name_string.c_str(), value);
        //     return;
        // }
        // else if (!statement.property_assignment.empty)
        // {
        //     struct parsed_property_assignment property_assignment = parsed_property_assignment_get(statement.property_assignment);
        //     struct parsed_property_setter property_setter = parsed_property_setter_get(property_assignment.property_setter);
        //     struct parsed_module_name module_name = parsed_module_name_get(property_setter.module_name);
        //     std::string module_name_string = identifier_to_string(module_name.identifier);
        //     if (!Global::modules.count(module_name_string))
        //     {
        //         printf("error: unknown module \"%s\"\n", module_name_string.c_str());
        //         return;
        //     }
        //     struct parsed_property_name property_name = parsed_property_name_get(property_setter.property_name);
        //     std::string property_name_string = identifier_to_string(property_name.identifier);
        //     Expression *expression = compile_expression(property_assignment.expression);
        //     Global::modules[module_name_string]->set(property_name_string, expression->evaluate());
        // }
        // else if (!statement.variable_name.empty)
        // {
        //     struct parsed_variable_name variable_name = parsed_variable_name_get(statement.variable_name);
        //     std::string variable_name_string = identifier_to_string(variable_name.identifier);
        //     if (!Global::variables.count(variable_name_string))
        //     {
        //         printf("error: unknown variable \"%s\"\n", variable_name_string.c_str());
        //         return;
        //     }
        //     printf("%f\n", Global::variables[variable_name_string]->value);
        // }
        // else if (!statement.variable_assignment.empty)
        // {
        //     struct parsed_variable_assignment variable_assignment = parsed_variable_assignment_get(statement.variable_assignment);
        //     struct parsed_variable_name variable_name = parsed_variable_name_get(variable_assignment.variable_name);
        //     std::string variable_name_string = identifier_to_string(variable_name.identifier);
        //     if (!Global::variables.count(variable_name_string))
        //     {
        //         Global::variables[variable_name_string] = new Variable();
        //     }
        //     Expression *expression = compile_expression(variable_assignment.expression);
        //     Global::variables[variable_name_string]->set(expression->evaluate());
        // }
        // else if (!statement.routine_definition.empty)
        // {
        //     struct parsed_routine_definition routine_definition = parsed_routine_definition_get(statement.routine_definition);
        //     struct parsed_routine_name routine_name = parsed_routine_name_get(routine_definition.routine_name);
        //     std::string routine_name_string = identifier_to_string(routine_name.identifier);
        //     if (Global::routines.count(routine_name_string))
        //     {
        //         printf("error: routine \"%s\" already exists\n", routine_name_string.c_str());
        //         return;
        //     }
        //     struct parsed_actions actions = parsed_actions_get(routine_definition.actions);
        //     Global::routines[routine_name_string] = new Routine(compile_actions(actions.action));
        // }
        // else if (!statement.rule_definition.empty)
        // {
        //     struct parsed_rule_definition rule_definition = parsed_rule_definition_get(statement.rule_definition);
        //     struct parsed_actions actions = parsed_actions_get(rule_definition.actions);
        //     Global::rules.push_back(new Rule(compile_condition(rule_definition.condition), new Routine(compile_actions(actions.action))));
        // }
        // else
        // {
        //     printf("error: unknown statement type\n");
        // }
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
    if (core_module->debug)
    {
        printf(">> %s\n", line);
        tic();
    }
    struct owl_tree *tree = owl_tree_create_from_string(line);
    if (core_module->debug)
    {
        toc("Tree creation");
    }
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
        if (core_module->debug)
        {
            owl_tree_print(tree);
            tic();
        }
        process_tree(tree);
        if (core_module->debug)
        {
            toc("Tree traversal");
        }
    }
    owl_tree_destroy(tree);
}

void app_main()
{
    Global::modules["core"] = core_module = new Core("core");

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
            if (rule->condition->evaluate_boolean() && !rule->routine->is_running())
            {
                rule->routine->start();
            }
            rule->routine->step();
        }

        for (auto const &item : Global::routines)
        {
            item.second->step();
        }
    }
}
