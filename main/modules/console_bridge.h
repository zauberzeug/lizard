#pragma once

#include "module.h"
#include <string>

// Dongle side of a console transport: forwards UART0 lines to a linked target and prints its console as our own
class ConsoleBridge : public Module {
public:
    ConsoleBridge(const std::string name);
    ~ConsoleBridge() override;

    // true while a line of the linked console is printed; echo callbacks must not send it anywhere else
    static bool printing_remote();

protected:
    std::string link_target; // "" if not linked

    void set_link(const std::string &target);
    // sends one UART0 line (without line end) to the linked target; called on the main task
    virtual void forward(const char *line, size_t len) = 0;
    static void print_remote(const char *line);

private:
    bool intercept(const char *line, int len);
};
