#include "console_bridge.h"
#include "../utils/uart.h"
#include <cstring>
#include <stdexcept>

static ConsoleBridge *active_bridge = nullptr;
static bool printing = false;

ConsoleBridge::ConsoleBridge(const std::string name) : Module(name) {
    if (active_bridge != nullptr) {
        throw std::runtime_error("only one console bridge can exist per node");
    }
    active_bridge = this;
    set_uart0_interceptor([this](const char *line, int len) { return this->intercept(line, len); });
}

// also runs when a derived constructor throws, so a failed bridge leaves no dangling interceptor
ConsoleBridge::~ConsoleBridge() {
    if (active_bridge == this) {
        active_bridge = nullptr;
        set_uart0_interceptor(nullptr);
    }
}

bool ConsoleBridge::printing_remote() {
    return printing;
}

void ConsoleBridge::set_link(const std::string &target) {
    this->link_target = target;
    this->get_property("link")->set_string_value(target);
}

void ConsoleBridge::print_remote(const char *line) {
    printing = true;
    echo("%s", line);
    printing = false;
}

bool ConsoleBridge::intercept(const char *line, int len) {
    if (this->link_target.empty()) {
        return false;
    }
    const size_t name_len = this->name.size();
    if ((size_t)len > name_len && strncmp(line, this->name.c_str(), name_len) == 0 && line[name_len] == '.') {
        return false; // the bridge's own methods stay local
    }
    this->forward(line, len);
    return true;
}
