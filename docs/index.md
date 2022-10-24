# Lizard

Lizard is a domain-specific language to define hardware behavior.
It is intended to run on embedded systems which are connected to motor controllers, sensors etc.
Most of the time it is used in combination with a higher level engine like [ROS](https://www.ros.org/) or [RoSys](http://rosys.io/).
You can think of the microcontroller as the machine's lizard brain which ensures basic safety and performs all time-critical actions.

The idea is to not compile and deploy specific C++ code for every new hardware requirement.
Instead you simply write your commands in a text-based language which can be changed on the fly.

## Features

- Shell-like input and output over serial communication to ease the development
- Quick and resource-efficient execution on embedded hardware
- Built-in checksums to detect transmission errors
- Safety conditions that are checked continuously
- Ability to work across multiple microcontrollers
- Human-readable and easy-to-type (like Python)
- Persistent "startup" commands to apply configuration after boot

## Concept

Lizard consists of individual hardware modules that are either defined in a persistent startup script or interactively via console input.
Each module has a name, a number of required or optional constructor arguments and possibly a step function as well as a number of properties.

Furthermore, Lizard allows to define _rules_, possibly asynchronous _routines_ and _variables_.
A routine is simply a collection of Lizard statements.
If a rule's condition evaluates to true, the corresponding routine is started.
Conditions can involve variables, module _properties_ and constant expressions.

One `core` module is automatically defined first.
It provides interaction with the microcontroller itself, e.g. reading the system's time or causing a restart.

During a main loop Lizard repeatedly performs the following tasks:

1. Read and evaluate input from the serial interface.
2. Run the step functions of each module. (The `core` module is evaluated last.)
3. Check all rules and execute associated routines.
4. Advance routines that are already running and waiting for certain conditions.
