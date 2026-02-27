# AI Agent Guidelines

> **For**: AI assistants (Cursor, GitHub Copilot, Codex, etc.)\
> **About**: The project, setup and usage is described in [README.md](README.md)\
> **Standards**: All coding standards are in [CONTRIBUTING.md](CONTRIBUTING.md) – follow those rules\
> **ESP-IDF Docs**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/\
> **Lizard Docs**: https://lizard.dev

---

## Project Overview

Lizard is a **domain-specific language (DSL)** for defining and controlling hardware behavior on ESP32 microcontrollers. It acts as a "lizard brain" for robots – handling time-critical actions, basic safety, and hardware communication while a higher-level system (ROS, RoSys) handles complex logic.

**Tech Stack**: C++17 on ESP-IDF, with Python tooling for build/flash.

**Related Projects**:
- [RoSys](https://github.com/zauberzeug/rosys) – Robot System framework (Python, uses Lizard)
- [NiceGUI](https://github.com/zauberzeug/nicegui) – Web UI framework often used with RoSys

---

## Lizard DSL Quick Reference

The Lizard language controls hardware via serial commands or startup scripts:

```python
# Module creation
led = Output(15)              # GPIO output on pin 15
motor = ODriveMotor(can, 0)   # ODrive motor on CAN bus

# Method calls
led.on()
led.off()
motor.power(0.5)

# Property access/assignment
motor.position                # Read property
motor.speed = 100             # Write property

# Variables
int count = 0
float speed = 3.14
bool active = true

# Rules (checked every cycle)
when button.level == 0 then
    led.on()
    count = count + 1
end

# Routines (reusable action sequences)
let blink do
    int t
    t = core.millis
    led.on()
    await core.millis > t + 500
    led.off()
end

# Control commands
!+led = Output(15)    # Add to startup script
!-led                 # Remove from startup
!.                    # Save startup to flash
!?                    # Print startup script
```

---

## Available Module Types

| Category | Modules |
|----------|---------|
| **Core** | `Core` (always present) |
| **I/O** | `Input`, `Output`, `PwmOutput`, `Analog`, `AnalogUnit` |
| **Communication** | `Serial`, `SerialBus`, `Can`, `Bluetooth`, `Expander` |
| **Motor Controllers** | `LinearMotor`, `ODriveMotor`, `ODriveWheels`, `RmdMotor`, `RmdPair`, `StepperMotor`, `RoboClawMotor`, `RoboClawWheels`, `D1Motor`, `DunkerMotor`, `DunkerWheels` |
| **CANopen** | `CanOpenMaster`, `CanOpenMotor` |
| **Sensors** | `Imu`, `TemperatureSensor` |
| **Expanders** | `Mcp23017` (I2C GPIO expander) |
| **Utilities** | `MotorAxis`, `Proxy` |

---

## Project Architecture

```
lizard/
├── main/                    # Core application
│   ├── main.cpp            # Entry point (app_main), main loop, UART processing
│   ├── global.cpp/h        # Global state (modules, variables, routines, rules)
│   ├── storage.cpp/h       # Non-volatile storage for startup scripts
│   ├── parser.h            # Generated parser (from language.owl via gen_parser.sh)
│   ├── compilation/        # DSL compilation (expressions, variables, routines, rules)
│   ├── modules/            # Hardware modules (motors, sensors, I/O, CAN, etc.)
│   └── utils/              # Utilities (UART, OTB updates, timing, string helpers)
├── components/             # ESP-IDF components and submodules
├── docs/                   # MkDocs documentation
├── examples/               # Usage examples (ROS integration, trajectories)
├── build.py               # Build script (wraps idf.py)
├── flash.py               # Flash script
├── monitor.py             # Serial monitor
├── language.owl           # Lizard grammar definition (Owl parser generator)
└── gen_parser.sh          # Regenerates parser.c from language.owl
```

### Key Entry Points

| File | Purpose |
|------|---------|
| `main/main.cpp` | `app_main()` – initialization, main loop |
| `main/modules/module.cpp` | `Module::create()` – module factory |
| `main/global.cpp` | Global state management |
| `language.owl` | DSL grammar definition |

---

## Development Commands

### Build

```bash
# Build for ESP32 (default)
python build.py

# Build for ESP32-S3
python build.py esp32s3

# Clean build
python build.py --clean
python build.py esp32s3 --clean
```

### Flash & Monitor

```bash
# Flash to connected device
python flash.py

# Monitor serial output
python monitor.py

# Or use idf.py directly
idf.py flash monitor
```

### Parser Regeneration

After modifying `language.owl`:

```bash
./gen_parser.sh
```

---

## Adding a New Module

Follow this pattern (see existing modules in `main/modules/`):

1. **Create header** (`my_module.h`):

```cpp
#pragma once
#include "module.h"

class MyModule;
using MyModule_ptr = std::shared_ptr<MyModule>;

class MyModule : public Module {
public:
    MyModule(const std::string name, /* constructor args */);
    void step() override;
    void call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) override;
    std::string get_output() const override;
};
```

2. **Implement** (`my_module.cpp`):

```cpp
#include "my_module.h"

MyModule::MyModule(const std::string name, /* args */)
    : Module(my_module, name) {
    // Initialize properties
    this->properties["some_property"] = std::make_shared<NumberVariable>(0.0);
}

void MyModule::step() {
    Module::step();  // Handle output_on and broadcast
    // Per-cycle logic
}

void MyModule::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "my_method") {
        Module::expect(arguments, 1, numbery);
        // Implementation
    } else {
        Module::call(method_name, arguments);  // Delegate to base
    }
}
```

3. **Register in module factory** (`module.cpp`):
   - Add to `ModuleType` enum in `module.h`
   - Add creation logic in `Module::create()` in `module.cpp`
   - Include the header in `module.cpp`

4. **Document in `docs/module_reference.md`**:

```markdown
## My Module

Brief description of what the module does.

| Constructor                 | Description | Arguments        |
| --------------------------- | ----------- | ---------------- |
| `my = MyModule(arg1, arg2)` | Description | `type1`, `type2` |

| Properties     | Description | Data type |
| -------------- | ----------- | --------- |
| `my.some_prop` | Description | `float`   |

| Methods             | Description | Arguments |
| ------------------- | ----------- | --------- |
| `my.some_method(x)` | Description | `float`   |
```

---

## Core Principles

### Think from First Principles

Don't settle for the first solution.
Question assumptions and think deeply about the true nature of the problem before implementing.

### Pair Programming Approach

We work together as pair programmers, switching seamlessly between driver and navigator:

- **Requirements first**: Verify requirements are correct before implementing
- **Discuss strategy**: Present options and trade-offs when uncertain about approach
- **Step-by-step for large changes**: Break down significant refactorings and get confirmation at each step
- **Challenge assumptions**: If the user makes wrong assumptions, correct them directly

### Discuss Before Implementing

For significant changes:

- Present the problem and possible approaches
- Discuss trade-offs and implications
- Get confirmation before proceeding with large refactorings
- Work iteratively with feedback at each step

### Simplicity First

- Prefer simple, straightforward solutions
- Avoid over-engineering
- Remove obsolete code rather than working around it
- Code should be self-explanatory

---

## Boundaries

### Do Without Asking

- Read any file in the codebase
- Run build commands (`python build.py`, `idf.py build`)
- Search for patterns and understand code
- Suggest refactorings or improvements
- Fix obvious bugs or typos

### Ask Before Doing

- Modifying the grammar (`language.owl`) – affects entire DSL
- Adding new dependencies or components
- Changing module interfaces (breaking changes)
- Modifying hardware pin assignments or defaults
- Large refactorings spanning multiple files
- Git operations (commit, push, branch)

---

## What to Avoid

- **Global mutable state** without clear justification
- **Raw pointers** for ownership – use smart pointers (`std::shared_ptr`, `std::unique_ptr`)
- **Memory leaks** – ensure all allocations are freed, prefer RAII patterns
- **Heap allocation in ISRs** – never allocate memory in interrupt context
- **Blocking operations** in the main loop or time-critical code paths
- **Debug prints** – use `echo()` for user-facing output, remove before committing
- **Unnecessary complexity** – follow existing patterns in the codebase
- **Code duplication** – check existing modules for similar functionality
- **Unrelated changes** – stay focused on the requested task

---

## Common Pitfalls

### Main Loop Timing
The main loop runs every **10ms** (`delay(10)` in `app_main`). Any operation that takes longer will delay all modules, rules, and routines. Avoid:
- Blocking I/O operations
- Long computations
- Waiting for external responses synchronously

### Module Registration
When adding a new module, you must:
1. Add to `ModuleType` enum in `module.h`
2. Add creation case in `Module::create()` in `module.cpp`
3. Include the header in `module.cpp`

Forgetting any step causes "unknown module type" errors.

### Argument Validation
Always use `Module::expect()` to validate constructor/method arguments:
```cpp
Module::expect(arguments, 2, identifier, integer);  // Exactly 2 args
Module::expect(arguments, -1, numbery, numbery);    // Variable count, validate types only
```

### Base Class Calls
Always call parent implementations:
- `Module::step()` – handles `output_on` and `broadcast` flags
- `Module::call()` – handles `mute`, `unmute`, `shadow`, `broadcast` methods

### Parser Regeneration
After modifying `language.owl`, always run `./gen_parser.sh`. The `parser.c` file is **generated** – never edit it directly.

---

## Error Handling Patterns

### User-Facing Output
Use `echo()` for messages sent to the serial console:
```cpp
echo("error in module \"%s\": %s", module->name.c_str(), e.what());
echo("warning: Checksum mismatch");
```

### Exceptions
Use `std::runtime_error` for recoverable errors that should be reported:
```cpp
throw std::runtime_error("module \"" + name + "\" is no serial connection");
throw std::runtime_error("unknown method \"" + this->name + "." + method_name + "\"");
```

### Error Wrapping
The main loop catches exceptions per-module to prevent one failing module from crashing others:
```cpp
try {
    module->step();
} catch (const std::runtime_error &e) {
    echo("error in module \"%s\": %s", module->name.c_str(), e.what());
}
```

---

## Debugging

### Enable Debug Mode
In a Lizard session:
```
core.debug = true
```
This prints parsing details and timing information.

### Serial Monitor
```bash
python monitor.py
# or
idf.py monitor
```

### Core Dumps
If the device crashes, use `core_dumper.py` to analyze:
```bash
python core_dumper.py
```

### Common Debug Techniques
- Add temporary `echo()` calls (remove before committing)
- Check `core.millis` for timing issues
- Use `module.unmute()` / `module.mute()` to control output
- Enable `module.broadcast()` to see all property changes

### ESP-IDF Logging
For low-level debugging, use ESP-IDF logging macros:
```cpp
#include "esp_log.h"
static const char *TAG = "my_module";
ESP_LOGI(TAG, "Info message");
ESP_LOGW(TAG, "Warning message");
ESP_LOGE(TAG, "Error message");
```

---

## Quick Verification

Before claiming a task complete, verify:

1. Code compiles without warnings for both ESP32 and ESP32-S3?
2. Code follows style guidelines (see [CONTRIBUTING.md](CONTRIBUTING.md))?
3. No blocking operations in main loop?
4. Debug code removed?
5. Memory management correct (no leaks, no dangling pointers)?
6. New module registered in `Module::create()` if applicable?
7. New module documented in `docs/module_reference.md` if applicable?

---

## When Uncertain

- **Check existing modules** for similar patterns before inventing new ones
- **Read the docs** at https://lizard.dev for DSL syntax and module reference
- **Ask the user** by presenting options and trade-offs if strategy is unclear

---

## Code Review Guidelines

**Purpose**: Maximize signal/noise, maintain code quality, and offload maintainers.
Act as a _single, concise reviewer_.
Prefer one structured top-level comment with suggested diffs over many line-by-line nits.

**Standards Reference**: Before starting a review, internalize all coding standards in [CONTRIBUTING.md](CONTRIBUTING.md).

### Scope & Tone

- Audience: PR authors and maintainers
- Voice: concise, technical, actionable
- Output format: one summary + grouped findings (**BLOCKER**, **MAJOR**, **CLEANUP**) + **suggested diff** blocks

### Severity Mapping

#### BLOCKER (request changes)

1. **Memory safety**: buffer overflows, use-after-free, dangling pointers, memory leaks, null pointer dereferences
2. **Security/Secrets**: leaked credentials/keys, command injection
3. **Concurrency issues**: race conditions, missing synchronization, ISR-unsafe code
4. **Resource exhaustion**: stack overflow, heap fragmentation, unbounded allocations
5. **Breaking changes**: changes that break existing Lizard scripts or module interfaces
6. **Main loop blocking**: operations that would stall the 10ms loop cycle

#### MAJOR (fix before merge)

1. **Error-handling gaps**: unchecked return values, silent failures
2. **Unnecessary complexity**: simpler design meets requirements
3. **Resource hygiene**: unclosed handles, missing RAII, leaked FreeRTOS resources
4. **Platform issues**: hardcoded values without ESP32/ESP32-S3 guards
5. **Missing registration**: new module not added to `Module::create()` or `ModuleType` enum
6. **Missing documentation**: new module not documented in `docs/module_reference.md`

#### CLEANUP (suggest quick diffs)

1. **Readability**: complex logic without comments; magic numbers
2. **const correctness**: missing `const` on parameters or methods
3. **Naming**: inconsistent with existing module patterns

### Review Structure

**Summary** → Lead with motivation, explain changes, assess risk

**BLOCKER** → Critical issues with rationale

**MAJOR** → Issues to fix pre-merge

**CLEANUP** → Quick improvements

**Suggested diffs** → Apply only if trivial and safe

---

> This file complements [CONTRIBUTING.md](CONTRIBUTING.md).
> Maintainers: update this file as conventions evolve.
