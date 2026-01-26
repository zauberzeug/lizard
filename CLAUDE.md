# Claude Code Guidelines for Lizard

## Project Overview

Lizard is a **domain-specific language (DSL)** for defining and controlling hardware behavior on ESP32 microcontrollers. It acts as a "lizard brain" for robots - handling time-critical actions, basic safety, and hardware communication while a higher-level system (ROS, RoSys) handles complex logic.

**Tech Stack**: C++17 on ESP-IDF, Python tooling for build/flash
**Full docs**: https://lizard.dev
**ESP-IDF docs**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/

## Key Commands

```bash
# Build
python build.py           # Build for ESP32 (default)
python build.py esp32s3   # Build for ESP32-S3
python build.py --clean   # Clean build

# Flash & Monitor
python flash.py           # Flash to device
python monitor.py         # Serial monitor

# Parser regeneration (after modifying language.owl)
./gen_parser.sh
```

## Project Structure

```
lizard/
├── main/                    # Core application
│   ├── main.cpp            # Entry point, main loop, UART processing
│   ├── global.cpp/h        # Global state (modules, variables, routines, rules)
│   ├── storage.cpp/h       # Non-volatile storage for startup scripts
│   ├── parser.h            # Generated parser (DO NOT EDIT - from language.owl)
│   ├── compilation/        # DSL compilation (expressions, variables, routines, rules)
│   ├── modules/            # Hardware modules (motors, sensors, I/O, CAN, etc.)
│   └── utils/              # Utilities (UART, OTA, timing, string helpers)
├── components/             # ESP-IDF components and submodules
├── docs/                   # MkDocs documentation
├── language.owl            # Lizard grammar definition (Owl parser generator)
└── gen_parser.sh           # Regenerates parser.h from language.owl
```

## Code Style

- **Indentation**: 4 spaces, never tabs
- **Line length**: 120 characters max
- **Header guards**: Use `#pragma once`
- **Naming**:
  - Classes: `CamelCase` (e.g., `ODriveMotor`)
  - Functions/variables: `snake_case` (e.g., `get_output`)
  - Type aliases: `snake_case` with `_ptr` suffix (e.g., `Module_ptr`)
  - Enums: `snake_case` values (e.g., `ModuleType::odrive_motor`)
  - Constants: `UPPER_SNAKE_CASE`
- **Smart pointers**: Use `std::shared_ptr`, `std::unique_ptr` - avoid raw pointers for ownership

## Module Development

Every new module must:

1. **Inherit from `Module`** with appropriate `ModuleType`
2. **Call `Module::step()`** in derived `step()` for output/broadcast handling
3. **Use `Module::expect()`** to validate arguments
4. **Delegate unknown methods** to `Module::call()` for base functionality

### Registration Checklist

After creating a new module:
- [ ] Add to `ModuleType` enum in `module.h`
- [ ] Add creation logic in `Module::create()` in `module.cpp`
- [ ] Include header in `module.cpp`
- [ ] Document in `docs/module_reference.md`

### Constructor Pattern

```cpp
MyModule::MyModule(const std::string name, /* args */)
    : Module(my_module, name) {
    this->properties["my_prop"] = std::make_shared<NumberVariable>(0.0);
}
```

### Method Call Pattern

```cpp
void MyModule::call(const std::string method_name, const std::vector<ConstExpression_ptr> arguments) {
    if (method_name == "my_method") {
        Module::expect(arguments, 1, numbery);  // Validate: 1 arg, numeric
        // Implementation
    } else {
        Module::call(method_name, arguments);  // IMPORTANT: delegate to base
    }
}
```

### Argument Types for `Module::expect()`

| Type | Description |
|------|-------------|
| `integer` | int64_t |
| `numbery` | int or float (accepts both) |
| `string` | string literal |
| `identifier` | module/variable name |
| `-1` | variable argument count (validate types only) |

## Compilation & Parser

```
language.owl          → Grammar definition (Owl parser generator)
    ↓ gen_parser.sh
parser.h              → Generated parser (DO NOT EDIT)
    ↓
main.cpp              → process_tree() compiles AST to runtime objects
    ↓
compilation/*.cpp     → Expression, Variable, Action, Routine, Rule classes
```

**Key functions in main.cpp**: `compile_expression()`, `compile_arguments()`, `compile_actions()`, `process_tree()`, `process_lizard()`

### Adding New Expression Types

1. Add case to `compile_expression()` switch in `main.cpp`
2. Create Expression subclass in `compilation/expression.cpp`
3. If new grammar needed: modify `language.owl` then run `./gen_parser.sh`

### Adding New Statement Types

1. Modify grammar in `language.owl`
2. Run `./gen_parser.sh` to regenerate parser
3. Add handling in `process_tree()` switch in `main.cpp`

## Python Tooling

Python scripts are **build/flash/monitor tools**, not the main application.

| Script | Purpose |
|--------|---------|
| `build.py` | Build for ESP32/ESP32-S3 (wraps `idf.py build`) |
| `flash.py` | Flash firmware to device |
| `monitor.py` | Serial monitor |
| `espresso.py` | OTA deployment tool |
| `configure.py` | Project configuration |
| `core_dumper.py` | Analyze core dumps |

**Python style**: single quotes, 120 char lines, `argparse` for CLI, `subprocess.run()` for shell commands, `pathlib.Path` for file operations

**Note**: These scripts wrap ESP-IDF tools. Ensure `IDF_PATH` is set and ESP-IDF is activated before running.

## What to Avoid

- **Global mutable state** without justification
- **Raw pointers** for ownership - use smart pointers
- **Memory leaks** - ensure all allocations freed, prefer RAII
- **Heap allocation in ISRs** - never allocate in interrupt context
- **Blocking operations** in main loop (runs every 10ms)
- **Debug prints** - use `echo()` for user output, remove before committing
- **Editing `parser.h`** - it's generated from `language.owl`

## Boundaries

### Do Without Asking

- Read any file in the codebase
- Run build commands (`python build.py`)
- Search for patterns and understand code
- Suggest refactorings or improvements
- Fix obvious bugs or typos

### Ask Before Doing

- Modifying the grammar (`language.owl`) - affects entire DSL
- Adding new dependencies or components
- Changing module interfaces (breaking changes)
- Modifying hardware pin assignments
- Large refactorings spanning multiple files
- Git operations (commit, push, branch)

## Error Handling

```cpp
// User-facing output
echo("error in module \"%s\": %s", module->name.c_str(), e.what());

// Recoverable errors
throw std::runtime_error("module \"" + name + "\" is no serial connection");
```

## Debugging

```bash
# Enable debug mode in Lizard session
core.debug = true

# Monitor serial output
python monitor.py

# Analyze core dumps
python core_dumper.py
```

## Verification Checklist

Before completing a task:

1. Code compiles without warnings for both ESP32 and ESP32-S3?
2. Code follows style guidelines?
3. No blocking operations in main loop?
4. Debug code removed?
5. Memory management correct?
6. New module registered if applicable?
7. Documentation updated if applicable?
