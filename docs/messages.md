# Messages
## Format

A message contains the module, which is addressed, a command and (optionally) one or more comma-separated arguments:

	<module> <command> [<arguments>]

Here are a few examples:

	esp restart
	bluetooth configure Z42
	wheels power 0.1,0.1
	motors move 0.5,0.5,2
	red_led on

We could extend this pattern to cover the following features:

A message ID could be used to confirm the reception of individual messages or to respond with requested data. (This could be also achieved based on the combination of module and command.)
A sender or receiver ID could allow for passing messages to additional devices like a second ESP.

These extensions, however, have the disadvantage of a more complicated format, which is cumbersome to type manually during development and debugging. But maybe we find a hybrid format: If the message starts with a digit, the first words are interpreted as message ID, sender and/or receiver.

## Checksum (optional)

A message can optionally contain a checksum that is checked upon reception. If it does not match, a warning is printed and the message is ignored. The sum is computed as XOR of all characters and appended with a “^” as a separator.
Example:
	Message:	led on
	Checksum:	0x6c ^ 0x65 ^ 0x64 ^ 0x20 ^ 0x6f ^ 0x6e = 76
	Result:		led on^76

All outgoing messages do contain a checksum.

## Types

### Constructors

Constructors define new modules using the new command, the module type, the module name and module-specific parameters.

    new <type> <name> [<param1>,[<param2>,[...]]]

### Settings

Settings begin with the keyword set:

    set <name>.<key>=<value>

Commands are sent to the module directly.

<name> <command> <arguments>

### Configurations

Configuration lines are indicated with special prefix characters and modify the commands in the persistent storage (NVS, namespace “storage”, key “main”). These commands are executed after booting the ESP. To prevent infinite boot loops due to faulty configuration lines, the configuration is ignored after three failed attempts.

	+<line>		    add a new line
	-<substring>	remove all lines starting with this substring
	?<substring>	print all lines starting with this substring

Note that empty substrings affect all lines.

## Conditions

Conditions can define command messages that are interpreted and executed when a triggering module is in a certain state.

	if <trigger> == <state> <msg>
	if <trigger> != <state> <msg>
	if <trigger1> == <state1> && <trigger2> == <state2> <msg>

## Shadows

Shadows can define equal behavior between different modules. The arrow specifies the direction, either one-way or two-way.

	shadow <module1> > <module2>
	shadow <module1> <> <module2>

# Multiple messages on one line

A line can contain multiple messages separated by a semicolon “;” (without additional whitespace):
red_led on;green_led on

# Responses

In cases where a command requests certain information, the module prints the response to the serial terminal. The output line starts with the module name and the given command, followed by the comma-separated response arguments.

The following example shows input and output to read the input pin A of a generic IO port.

	Input:		io4 readA
	Output:		io4 readA 1

Note that many modules automatically print various status information once every loop cycle. This behaviour can be switched on or off during configuration.
