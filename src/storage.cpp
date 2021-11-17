#include "storage.h"

#include <string>
#include "nvs_flash.h"
#include "utils/strings.h"

#define NAMESPACE "storage"
#define KEY "main"

std::string Storage::startup;

void Storage::init()
{
    nvs_flash_init();
    Storage::startup = Storage::get();
}

void write(std::string ns, std::string key, std::string value)
{
    nvs_handle handle;
    if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) != ESP_OK)
    {
        printf("Could not open storage namespace: %s\n", ns.c_str());
        return;
    }
    if (nvs_set_str(handle, key.c_str(), value.c_str()) != ESP_OK)
    {
        printf("Could not write to storage: %s.%s=%s\n", ns.c_str(), key.c_str(), value.c_str());
        nvs_close(handle);
        return;
    }
    if (nvs_commit(handle) != ESP_OK)
    {
        printf("Could not commit storage: %s.%s=%s\n", ns.c_str(), key.c_str(), value.c_str());
        nvs_close(handle);
        return;
    }
    nvs_close(handle);
}

std::string read(std::string ns, std::string key)
{
    nvs_handle handle;
    if (nvs_open(ns.c_str(), NVS_READWRITE, &handle) != ESP_OK)
    {
        printf("Could not open storage namespace: %s\n", ns.c_str());
        return "";
    }
    size_t size = 0;
    if (nvs_get_str(handle, key.c_str(), NULL, &size) != ESP_OK)
    {
        printf("Could not peek storage: %s.%s\n", ns.c_str(), key.c_str());
        nvs_close(handle);
        return "";
    }
    char *value = (char *)malloc(size);
    if (size > 0)
    {
        if (nvs_get_str(handle, key.c_str(), value, &size) != ESP_OK)
        {
            printf("Could not read storage: %s.%s\n", ns.c_str(), key.c_str());
            free(value);
            nvs_close(handle);
            return "";
        }
    }
    std::string result = std::string(value);
    free(value);
    nvs_close(handle);
    return result;
}

void Storage::put(std::string value)
{
    write(NAMESPACE, KEY, value);
}

std::string Storage::get()
{
    return read(NAMESPACE, KEY);
}

void Storage::append_to_startup(std::string line)
{
    Storage::startup += line + '\n';
}

void Storage::remove_from_startup(std::string substring)
{
    std::string new_startup = "";
    while (!Storage::startup.empty())
    {
        std::string line = cut_first_word(Storage::startup, '\n');
        if (!starts_with(line, substring))
        {
            new_startup += line + '\n';
        }
    }
    Storage::startup = new_startup;
}

void Storage::print_startup(std::string substring)
{
    std::string startup = Storage::startup;
    while (!startup.empty())
    {
        std::string line = cut_first_word(startup, '\n');
        if (starts_with(line, substring))
        {
            printf("%s\n", line.c_str());
        }
    }
}

void Storage::save_startup()
{
    Storage::put(Storage::startup);
}