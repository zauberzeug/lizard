#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Storage {
private:
    // The startup script lives in NVS and is only held in RAM while it is edited (from the first `!+` or `!-` to `!.`),
    // in pieces of at most one NVS chunk, so that neither booting nor editing needs one large block of heap.
    static bool editing;
    static std::vector<std::string> edit_pieces;

    static void read_stored(const std::function<void(const std::string &piece)> &piece);
    static void begin_edit(const bool keep_current);
    static void append_to_edit(const char *text, const size_t length);
    static void nvs_delete_key(const std::string &ns, const std::string &key);

public:
    static void init();

    // Calls `piece` with the startup script in order, in pieces of at most one NVS chunk; a line may span two pieces.
    static void read_startup(const std::function<void(const std::string &piece)> &piece);
    // Calls `line` with every line of the startup script, without its '\n'.
    static void read_startup_lines(const std::function<void(const std::string &line)> &line);
    // The byte sum of the stored script, as it will boot; an unsaved edit does not count.
    static std::uint16_t startup_checksum();

    static void append_to_startup(const std::string &line);
    static void remove_from_startup(const std::string &prefix = "");
    static void print_startup(const std::string &prefix = "");
    static void save_startup();

    static void set_user_pin(const std::uint32_t pin);
    static bool get_user_pin(std::uint32_t &pin);
    static void remove_user_pin();

    static void set_baudrate(const std::uint32_t baudrate);
    static bool get_baudrate(std::uint32_t &baudrate);
};
