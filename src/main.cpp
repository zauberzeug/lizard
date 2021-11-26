#include <chrono>
#include <stdexcept>
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <string.h>
#include <vector>
#include "driver/gpio.h"
#include "driver/uart.h"

#include "compilation/await_condition.h"
#include "compilation/await_routine.h"
#include "compilation/expression.h"
#include "compilation/method_call.h"
#include "compilation/routine.h"
#include "compilation/routine_call.h"
#include "compilation/rule.h"
#include "compilation/variable.h"
#include "compilation/variable_assignment.h"
#include "modules/core.h"
#include "modules/module.h"
#include "utils/output.h"
#include "utils/tictoc.h"
#include "utils/timing.h"
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

Expression *compile_expression(struct owl_ref ref);

std::vector<Expression *> compile_arguments(struct owl_ref ref)
{
    std::vector<Expression *> arguments;
    for (struct owl_ref r = ref; !r.empty; r = owl_next(r))
    {
        arguments.push_back(compile_expression(r));
    }
    return arguments;
}

Expression *compile_expression(struct owl_ref ref)
{
    struct parsed_expression expression = parsed_expression_get(ref);
    switch (expression.type)
    {
    case PARSED_TRUE:
        return new BooleanExpression(true);
    case PARSED_FALSE:
        return new BooleanExpression(false);
    case PARSED_STRING:
    {
        struct parsed_string string = parsed_string_get(expression.string);
        return new StringExpression(std::string(string.string, string.length));
    }
    case PARSED_INTEGER:
        return new IntegerExpression(parsed_integer_get(expression.integer).integer);
    case PARSED_NUMBER:
        return new NumberExpression(parsed_number_get(expression.number).number);
    case PARSED_VARIABLE:
        return new VariableExpression(Global::get_variable(identifier_to_string(expression.identifier)));
    case PARSED_PROPERTY:
        return new VariableExpression(Global::get_module(identifier_to_string(expression.module_name))
                                          ->get_property(identifier_to_string(expression.property_name)));
    case PARSED_PARENTHESES:
        return compile_expression(expression.expression);
    case PARSED_POWER:
        return new PowerExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_NEGATE:
        return new NegateExpression(compile_expression(expression.operand));
    case PARSED_MULTIPLY:
        return new MultiplyExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_DIVIDE:
        return new DivideExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_ADD:
        return new AddExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_SUBTRACT:
        return new SubtractExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_SHIFT_LEFT:
        return new ShiftLeftExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_SHIFT_RIGHT:
        return new ShiftRightExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_BIT_AND:
        return new BitAndExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_BIT_XOR:
        return new BitXorExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_BIT_OR:
        return new BitOrExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_GREATER:
        return new GreaterExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_LESS:
        return new LessExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_GREATER_EQUAL:
        return new GreaterEqualExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_LESS_EQUAL:
        return new LessEqualExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_EQUAL:
        return new EqualExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_UNEQUAL:
        return new UnequalExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_NOT:
        return new NotExpression(compile_expression(expression.operand));
    case PARSED_AND:
        return new AndExpression(compile_expression(expression.left), compile_expression(expression.right));
    case PARSED_OR:
        return new OrExpression(compile_expression(expression.left), compile_expression(expression.right));
    default:
        throw std::runtime_error("invalid expression");
    }
}

std::vector<Action *> compile_actions(struct owl_ref ref)
{
    std::vector<Action *> actions;
    for (struct owl_ref r = ref; !r.empty; r = owl_next(r))
    {
        struct parsed_action action = parsed_action_get(r);
        if (!action.noop.empty)
        {
        }
        else if (!action.method_call.empty)
        {
            struct parsed_method_call method_call = parsed_method_call_get(action.method_call);
            std::string module_name = identifier_to_string(method_call.module_name);
            Module *module = Global::get_module(module_name);
            std::string method_name = identifier_to_string(method_call.method_name);
            std::vector<Expression *> arguments = compile_arguments(method_call.argument);
            actions.push_back(new MethodCall(module, method_name, arguments));
        }
        else if (!action.routine_call.empty)
        {
            struct parsed_routine_call routine_call = parsed_routine_call_get(action.routine_call);
            std::string routine_name = identifier_to_string(routine_call.routine_name);
            Routine *routine = Global::get_routine(routine_name);
            actions.push_back(new RoutineCall(routine));
        }
        else if (!action.variable_assignment.empty)
        {
            struct parsed_variable_assignment variable_assignment = parsed_variable_assignment_get(action.variable_assignment);
            std::string variable_name = identifier_to_string(variable_assignment.variable_name);
            Variable *variable = Global::get_variable(variable_name);
            Expression *expression = compile_expression(variable_assignment.expression);
            actions.push_back(new VariableAssignment(variable, expression));
        }
        else if (!action.await_condition.empty)
        {
            struct parsed_await_condition await_condition = parsed_await_condition_get(action.await_condition);
            Expression *condition = compile_expression(await_condition.condition);
            actions.push_back(new AwaitCondition(condition));
        }
        else if (!action.await_routine.empty)
        {
            struct parsed_await_routine await_routine = parsed_await_routine_get(action.await_routine);
            std::string routine_name = identifier_to_string(await_routine.routine_name);
            Routine *routine = Global::get_routine(routine_name);
            actions.push_back(new AwaitRoutine(routine));
        }
        else
        {
            throw std::runtime_error("unknown action type");
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
        if (!statement.noop.empty)
        {
        }
        else if (!statement.expression.empty)
        {
            Expression *expression = compile_expression(statement.expression);
            static char buffer[256];
            expression->print_to_buffer(buffer);
            echo(all, text, buffer);
        }
        else if (!statement.constructor.empty)
        {
            struct parsed_constructor constructor = parsed_constructor_get(statement.constructor);
            std::string module_name = identifier_to_string(constructor.module_name);
            if (Global::has_module(module_name))
            {
                throw std::runtime_error("module \"" + module_name + "\" already exists");
            }
            std::string module_type = identifier_to_string(constructor.module_type);
            std::vector<Expression *> arguments = compile_arguments(constructor.argument);
            Global::add_module(module_name, Module::create(module_type, module_name, arguments));
        }
        else if (!statement.method_call.empty)
        {
            struct parsed_method_call method_call = parsed_method_call_get(statement.method_call);
            std::string module_name = identifier_to_string(method_call.module_name);
            Module *module = Global::get_module(module_name);
            std::string method_name = identifier_to_string(method_call.method_name);
            std::vector<Expression *> arguments = compile_arguments(method_call.argument);
            module->call_with_shadows(method_name, arguments);
        }
        else if (!statement.routine_call.empty)
        {
            struct parsed_routine_call routine_call = parsed_routine_call_get(statement.routine_call);
            std::string routine_name = identifier_to_string(routine_call.routine_name);
            Routine *routine = Global::get_routine(routine_name);
            if (routine->is_running())
            {
                throw std::runtime_error("routine \"" + routine_name + "\" is already running");
            }
            routine->start();
        }
        else if (!statement.property_assignment.empty)
        {
            struct parsed_property_assignment property_assignment = parsed_property_assignment_get(statement.property_assignment);
            std::string module_name = identifier_to_string(property_assignment.module_name);
            Module *module = Global::get_module(module_name);
            std::string property_name = identifier_to_string(property_assignment.property_name);
            Expression *expression = compile_expression(property_assignment.expression);
            module->write_property(property_name, expression);
        }
        else if (!statement.variable_assignment.empty)
        {
            struct parsed_variable_assignment variable_assignment = parsed_variable_assignment_get(statement.variable_assignment);
            std::string variable_name = identifier_to_string(variable_assignment.variable_name);
            Variable *variable = Global::get_variable(variable_name);
            variable->assign(compile_expression(variable_assignment.expression));
        }
        else if (!statement.variable_declaration.empty)
        {
            struct parsed_variable_declaration variable_declaration = parsed_variable_declaration_get(statement.variable_declaration);
            struct parsed_datatype datatype = parsed_datatype_get(variable_declaration.datatype);
            std::string variable_name = identifier_to_string(variable_declaration.variable_name);
            switch (datatype.type)
            {
            case PARSED_BOOLEAN:
                Global::add_variable(variable_name, new BooleanVariable());
                break;
            case PARSED_INTEGER:
                Global::add_variable(variable_name, new IntegerVariable());
                break;
            case PARSED_NUMBER:
                Global::add_variable(variable_name, new NumberVariable());
                break;
            case PARSED_STRING:
                Global::add_variable(variable_name, new StringVariable());
                break;
            default:
                throw std::runtime_error("invalid data type for variable declaration");
            }
            if (!variable_declaration.expression.empty)
            {
                Global::get_variable(variable_name)->assign(compile_expression(variable_declaration.expression));
            }
        }
        else if (!statement.routine_definition.empty)
        {
            struct parsed_routine_definition routine_definition = parsed_routine_definition_get(statement.routine_definition);
            std::string routine_name = identifier_to_string(routine_definition.routine_name);
            if (Global::has_routine(routine_name))
            {
                throw std::runtime_error("routine \"" + routine_name + "\" already exists");
            }
            struct parsed_actions actions = parsed_actions_get(routine_definition.actions);
            Global::add_routine(routine_name, new Routine(compile_actions(actions.action)));
        }
        else if (!statement.rule_definition.empty)
        {
            struct parsed_rule_definition rule_definition = parsed_rule_definition_get(statement.rule_definition);
            struct parsed_actions actions = parsed_actions_get(rule_definition.actions);
            Routine *routine = new Routine(compile_actions(actions.action));
            Expression *condition = compile_expression(rule_definition.condition);
            Global::add_rule(new Rule(condition, routine));
        }
        else
        {
            throw std::runtime_error("unknown statement type");
        }
    }
}

void process_lizard(const char *line)
{
    bool debug = core_module->get_property("debug")->boolean_value;
    if (debug)
    {
        echo(all, text, ">> %s", line);
        tic();
    }
    struct owl_tree *tree = owl_tree_create_from_string(line);
    if (debug)
    {
        toc("Tree creation");
    }
    struct source_range range;
    switch (owl_tree_get_error(tree, &range))
    {
    case ERROR_INVALID_FILE:
        echo(all, text, "error: invalid file");
        break;
    case ERROR_INVALID_OPTIONS:
        echo(all, text, "error: invalid options");
        break;
    case ERROR_INVALID_TOKEN:
        echo(all, text, "error: invalid token at range %zu %zu \"%s\"", range.start, range.end,
             std::string(&line[range.start], range.end - range.start));
        break;
    case ERROR_UNEXPECTED_TOKEN:
        echo(all, text, "error: unexpected token at range %zu %zu \"%s\"", range.start, range.end,
             std::string(&line[range.start], range.end - range.start));
        break;
    case ERROR_MORE_INPUT_NEEDED:
        echo(all, text, "error: more input needed at range %zu %zu", range.start, range.end);
        break;
    default:
        if (debug)
        {
            owl_tree_print(tree);
            tic();
        }
        process_tree(tree);
        if (debug)
        {
            toc("Tree traversal");
        }
    }
    owl_tree_destroy(tree);
}

void process_line(const char *line, int len, uart_port_t uart_num)
{
    if (len >= 2 && line[0] == '!')
    {
        switch (line[1])
        {
        case '+':
            Storage::append_to_startup(line + 2);
            break;
        case '-':
            Storage::remove_from_startup(line + 2);
            break;
        case '?':
            Storage::print_startup(line + 2);
            break;
        case '.':
            Storage::save_startup();
            break;
        case '!':
            process_lizard(line + 2);
            break;
        case '"':
            echo(uart_num == UART_NUM_1 ? uart0 : all, text, line + 2);
            break;
        default:
            throw std::runtime_error("unrecognized control command");
        }
    }
    else
    {
        if (uart_num == UART_NUM_1)
        {
            printf("%s\n", line);
        }
        else
        {
            process_lizard(line);
        }
    }
}

void process_uart(uart_port_t uart_num)
{
    static char *input = (char *)malloc(BUFFER_SIZE);
    static int buffer_start = 0;
    static uint8_t checksum = 0;

    try
    {
        size_t size;
        uart_get_buffered_data_len(uart_num, &size);
        if (size == 0)
        {
            return;
        }
        // printf("*** there are %d bytes in the ring buffer\n", size);
        if (size >= BUFFER_SIZE - buffer_start)
        {
            uart_flush_input(uart_num);
            throw std::runtime_error("uart buffer overflow, flushing");
        }

        int len = uart_read_bytes(uart_num, (uint8_t *)input + buffer_start, size, 0);
        // printf("*** got %d bytes via uart_read_bytes\n", len);

        int line_start = 0;
        int pos = 0;
        while (pos < buffer_start + len)
        {
            switch (input[pos])
            {
            case '@':
            {
                // printf("*** found '@' at position %d\n", pos);
                std::string hex_number(&input[pos + 1], 2);
                int number = std::stoi(hex_number, 0, 16);
                // printf("*** number: %c %c\n", input[pos + 1], input[pos + 2]);
                input[pos] = 0;
                if (number == checksum)
                {
                    // printf("*** process line: \"%s\"\n", &input[line_start]);
                    process_line(&input[line_start], pos - line_start, uart_num);
                }
                else
                {
                    echo(uart0, text, "error: checksum mismatch for input \"%s\" (%d vs. %d)",
                         &input[line_start], number, checksum);
                }
                pos += 4;
                line_start = pos;
                checksum = 0;
                break;
            }
            case '\0':
            case '\n':
                // printf("*** found chr(%d) at position %d\n", input[pos], pos);
                input[pos] = 0;
                // printf("*** process line: \"%s\"\n", &input[line_start]);
                process_line(&input[line_start], pos - line_start, uart_num);
                pos += 1;
                line_start = pos;
                break;
            default:
                // printf("*** char %d\n", input[pos]);
                checksum ^= input[pos];
                pos += 1;
            }
        }

        // printf("*** input buffer: \"%s\" (length %d)\n", std::string(input, pos).c_str(), pos);
        // printf("*** moving %d bytes from offset %d to 0\n", pos - line_start, line_start);
        memmove(input, input + line_start, pos - line_start);
        buffer_start = pos - line_start;
        pos -= line_start;
        // printf("*** input buffer: \"%s\" (length %d)\n", std::string(input, pos).c_str(), pos);
        // printf("*** buffer_start = %d\n", buffer_start);
    }
    catch (const std::runtime_error &e)
    {
        echo(all, text, "error while processing line from UART %d: %s", uart_num, e.what());
        uart_flush_input(uart_num);
        buffer_start = 0;
        checksum = 0;
    }
}

void app_main()
{
    try
    {
        assert(Global::variables.size() == number_of_module_types);
        Global::add_module("core", core_module = new Core("core"));
    }
    catch (const std::runtime_error &e)
    {
        echo(all, text, "error while initializing global state variables and modules: %s", e.what());
        exit(1);
    }

    try
    {
        Storage::init();
        process_lizard(Storage::startup.c_str());
    }
    catch (const std::runtime_error &e)
    {
        echo(all, text, "error while loading startup script: %s", e.what());
    }

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
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, GPIO_NUM_27, GPIO_NUM_26, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, BUFFER_SIZE * 2, 0, 0, NULL, 0);

    while (true)
    {
        process_uart(UART_NUM_0);
        process_uart(UART_NUM_1);

        for (auto const &item : Global::modules)
        {
            try
            {
                item.second->step();
            }
            catch (const std::runtime_error &e)
            {
                echo(all, text, "error in module \"%s\": %s", item.first.c_str(), e.what());
            }
        }

        for (auto const &rule : Global::rules)
        {
            try
            {
                if (rule->condition->evaluate_boolean() && !rule->routine->is_running())
                {
                    rule->routine->start();
                }
                rule->routine->step();
            }
            catch (const std::runtime_error &e)
            {
                echo(all, text, "error in rule: %s", e.what());
            }
        }

        for (auto const &item : Global::routines)
        {
            try
            {
                item.second->step();
            }
            catch (const std::runtime_error &e)
            {
                echo(all, text, "error in routine \"%s\": %s", item.first.c_str(), e.what());
            }
        }

        delay(10);
    }
}
