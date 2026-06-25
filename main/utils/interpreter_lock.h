#pragma once

// RAII guard for the global interpreter mutex serializing all Lizard interpretation
// (main loop steps, UART/BLE command processing and the scheduler task).
// The mutex is recursive, so nested guards on the same task are safe.
class InterpreterLock {
public:
    InterpreterLock();
    ~InterpreterLock();

    InterpreterLock(const InterpreterLock &) = delete;
    InterpreterLock &operator=(const InterpreterLock &) = delete;
};
