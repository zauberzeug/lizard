# The Lizard Language

## Statements

Statements are input via the command-line interface or stored in the startup script on the microcontroller.
The following statement types are currently supported.

**Expressions**

An expression can be a constant value, a variable, a module property, an arithmetic or logical expression as well as various combinations:

    true
    42
    3.14
    "Hello world"
    led
    button.level
    1 + (2 - 3 * 4)**5
    1 == 2 or (x == 4 and button.level == 0)

Expressions can be assigned to variables, used in conditions and passed to constructors, method calls or routine calls.
Plain expression statements print their result to the command line.

**Variables: declaration and assignment**

New variables need to be explicitly declared with a data type:

    int i

They can be immediately initialized:

    int i = 1

Otherwise they have an initial value of `false`, `0`, `0.0` or `""`, respectively.

Variables can be assigned a new value of compatible data type:

    i = 2

**Modules: constructors, method calls and property assignments**

Constructors are used to create module instances:

    led = Output(15)

See the module reference for more details about individual modules and their argument lists.

Constructors can also be used with [expander modules](module_reference.md#expander) to instantiate a remote module that can be controlled from the main microcontroller:

    led = expander.Output(15)

You can call module methods as follows:

    led.on()

Some module properties are meant to be written to:

    motor.ratio = 9

**Routines: definition and call**

Routines have a name and contain a list of actions:

    let all_on do
        green.on()
        red.on()
    end

They can be called similar to module methods:

    all_on()

**Rules: definition**

Rules execute a list of actions when a condition is met:

    when button.level == 0 then
        core.print("Off!")
        led.off()
    end

In contrast to an if-statement known from other languages a when-condition is checked in every cycle of Lizard's main loop.
Whenever the condition is true, the actions are executed.

Note that actions can be asynchronous.
If there are still actions running asynchronously, a truthy condition is ignored.

## Actions

Routines and rules contain a list of actions.

**Method and routine calls**

Like with a method call statement, you can call methods and routines with an action:

    when clicked then
        core.print("On!")
        all_on()
    end

**Property and variable assignments**

Like with the corresponding assignment statements, you can assign properties and variables with an action as well:

    when i > 0 then
        i = 0
        core.debug = true
    end

**Await conditions and routines**

In contrast to statements, actions can _await_ conditions, causing the execution of subsequent actions to wait until the condition is met.

    int t
    let blink do
        t = core.millis
        led.on()
        await core.millis > t + 1000
        led.off()
    end

Similarly, actions can await asynchronous routines, causing subsequent actions to wait until a routine is completed.

    when button.level == 0 then
        core.print("Blink...")
        await blink()
        core.print("Done.")
    end

## Data types

Lizard currently supports five data types:

| Data type             | Example            | Range                   |
| --------------------- | ------------------ | ----------------------- |
| boolean               | `bool b = true`    | `false`, `true`         |
| integer number        | `int i = 0`        | 64-bit unsigned integer |
| floating point number | `float f = 0.0`    | 64-bit float            |
| string                | `str s = "foo"`    |
| identifier            | `led = Output(15)` |

Note that identifiers cannot be created via variable declarations, but only via constructors.

Implicit conversion only happens from integers to floating point numbers:

    int i = 42
    float f = i + 3.14

## Whitespace, comments and line breaks

Tabs and spaces are treated as whitespace.

Blank lines are interpreted as no-op and do nothing.

Line comments start with `//`.

Multiple statements or actions are separated with `;` or a newline.

## Control commands

Lines with a leading `!` can indicate one of the following control commands.

| Command | Meaning                                                  |
| ------- | -------------------------------------------------------- |
| `!+abc` | Add `abc` to the startup script                          |
| `!-abc` | Remove lines starting with `abc` from the startup script |
| `!?`    | Print the startup script                                 |
| `!.`    | Write the startup script to non-volatile storage         |
| `!!abc` | Interpret `abc` as Lizard code                           |
| `!"abc` | Print `abc` to the command-line                          |

Note that the commands `!+`, `!-` and `!?` affect the startup script in RAM, which is only written to non-volatile storage with the `!.` command.

Input from the default command-line interface UART0 is usually interpreted as Lizard code;
input from a [port expander](module_reference.md#expander) is usually printed to the command-line on UART0.
This behavior can be changed using `!!` and `!"`.
