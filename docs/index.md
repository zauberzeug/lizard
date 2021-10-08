# Lizard

Lizard is a domain-specific language to define and control hardware behaviour. It is intened to run on embedded systems which are connected to motor controllers, sensors etc.

# Features

- Shell-like input/output over serial to ease the development.
- Quick and ressource efficient execution on embedded hardware.
- Safety conditions are checked continously with realtime guarantees.
- Human-readable and easy to type (like bash).
- As short as possible to ensure quick transmission and interpretation.
- Arguments use SI base units if possible.

# Language overview

- Message: any text which is send to the microcontroller over serial
- Output: the text which is send from a module over serial
- Module: the internal representation for an piece of hardware which has commands, settings, configurations and triggers
- Command: a message which is send to an module
- Setting: a message which changes the behaviour of a module
- Configuration: messages which are stored persitently and executed after boot to initialize the system
- Trigger: a kind of variable through which a module reflects its hardware state
- Conditions: regulary checked statements which check a trigger for a certain type to execute a command