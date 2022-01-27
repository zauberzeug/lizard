# Control commands

Lines with a leading `!` can indicate one of the following control commands.

| Command | Meaning                                                  |
| ------- | -------------------------------------------------------- |
| `!+abc` | Add `abc` to the startup script                          |
| `!-abc` | Remove lines starting with `abc` from the startup script |
| `!?`    | Print the startup script                                 |
| `!.`    | Write the startup script to non-volatile storage         |
| `!!abc` | Interpret `abc` as Lizard code                           |
| `!"abc` | Print `abc` to the command-line                          |
| `!>abc` | Send `abc` via UART1 to another microcontroller          |
| `!~`    | Deactivate UART1 and set pins floating                   |

Note that the commands `!+`, `!-` and `!?` affect the startup script in RAM, which is only written to non-volatile storage with the `!.` command.

Input from the default command-line interface UART0 is usually interpreted as Lizard code;
input from another microcontroller connected on UART1 is usually printed to the command-line on UART0.
This behavior can be changed using `!!`, `!"` and `!>`.
