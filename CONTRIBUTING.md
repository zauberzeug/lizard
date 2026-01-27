# Contributing to Lizard

This document describes coding standards and contribution workflows for the Lizard project.

## Code Style

Lizard follows the [ESP-IDF Style Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/style-guide.html) with some project-specific conventions.

### General Formatting

- **Indentation**: 4 spaces, never tabs
- **Line length**: 120 characters maximum
- **Line endings**: LF (Unix style) only
- **Trailing whitespace**: None

### C++ Naming Conventions

| Element                  | Style                            | Example                          |
| ------------------------ | -------------------------------- | -------------------------------- |
| Classes/Structs          | `CamelCase`                      | `Module`, `ODriveMotor`          |
| Member variables/methods | `snake_case`                     | `get_output()`, `shadow_modules` |
| Local variables          | `snake_case`                     | `motor_id`, `can_name`           |
| Static variables         | `s_` prefix                      | `s_instance_count`               |
| Type aliases             | `snake_case` with `_ptr` or `_t` | `Module_ptr`, `signed_32_bit_t`  |
| Enums                    | `snake_case` values              | `ModuleType::odrive_motor`       |
| Namespaces               | `snake_case`                     | `compilation`                    |
| Constants/Macros         | `UPPER_SNAKE_CASE`               | `DEFAULT_SDA_PIN`                |

### File Naming

- C++ headers: `.h` (we use `.h` for consistency with ESP-IDF components)
- C++ sources: `.cpp`
- Python scripts: `snake_case.py`

### Header Files

Use `#pragma once` for include guards:

```cpp
#pragma once

#include <standard_library>
#include "project_header.h"

// declarations
```

### Braces

- Function definitions: opening brace on same line
- Conditionals/loops: opening brace on same line

```cpp
void function(int arg) {
    if (condition) {
        do_something();
    } else {
        do_other();
    }
}
```

### Comments

- Use `//` for single-line comments
- Use `/* */` for multi-line comments sparingly
- Prefix important notes with `NOTE:`
- Use `TODO:` for planned improvements
- Remove commented-out code (use git history instead)

### Class Member Order

1. `public` members first, then `protected`, then `private`
2. Within each: constructors/destructors, then methods, then variables

### Smart Pointers

Use smart pointers for ownership semantics:

```cpp
using Module_ptr = std::shared_ptr<Module>;
using ConstModule_ptr = std::shared_ptr<const Module>;
```

## ESP-IDF Specific

### Error Handling

- Use `ESP_ERROR_CHECK()` for unrecoverable errors
- Return `esp_err_t` for recoverable errors
- Use exceptions sparingly (disabled by default in ESP-IDF)

### Memory Management

- Prefer stack allocation where possible
- Use RAII for resource management
- Be mindful of heap fragmentation on embedded systems
- Never allocate memory in ISR context

### Thread Safety

- Use FreeRTOS primitives (`xSemaphore`, `xMutex`) for synchronization
- Mark ISR-callable functions appropriately
- Be aware of task priorities and potential priority inversion

## Python Tooling

For Python scripts (build tools, flash utilities):

- Use single quotes for strings
- Follow PEP 8 with 120 character line length
- Use type hints where helpful

## Git Workflow

### Commits

- Write clear, descriptive commit messages
- Focus on "why" not just "what"
- Keep commits focused on single logical changes

### Branches

- Create feature branches for new work
- Keep branches up to date with main

## Testing

- Test on actual hardware when possible
- Verify compilation for both ESP32 and ESP32-S3 targets
- Check for memory leaks and stack overflows

## Before Submitting

1. Code compiles without warnings
2. Follows style guidelines
3. Tested on target hardware
4. No debug prints left in code
5. Documentation updated if needed
