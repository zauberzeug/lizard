# Lizard

Lizard is a domain-specific language to define hardware behaviour.
It is intened to run on embedded systems which are connected to motor controllers, sensors etc.
Most of the time it is used in combination with a higher level engine like [ROS](https://www.ros.org/) or [RoSys](http://rosys.io/).
You can think of the microcontroller as the machine's lizard brain which ensures basic safety and performs all time critical actions.

## Features

- Shell-like input/output over serial to ease the development.
- Quick and ressource efficient execution on embedded hardware.
- Safety conditions are checked continously with realtime guarantees.
- Human-readable and easy to type (like bash).
- As short as possible to ensure quick transmission and interpretation.
- Arguments use SI base units if possible.

## Definitions

**Message**: any text which is send to the microcontroller over serial

**Output**: the text which is send from a module over serial

**Module**: the internal representation for an piece of hardware which has commands, settings, configurations and triggers

**Command**: a message which is send to an module

**Setting**: a message which changes the behaviour of a module

**Configuration**: messages which are stored persitently and executed after boot to initialize the system

**Trigger**: a kind of variable through which a module reflects its hardware state

**Conditions**: regulary checked statements which probe a trigger for a certain value which then executes a command
