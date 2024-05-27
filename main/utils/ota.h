#pragma once
#include <string>

namespace ota {

typedef struct {
    std::string ssid;
    std::string password;
    std::string url;
} ota_params_t;

bool setup_wifi(const char *ssid, const char *password);
void attempt(const char *url);
void verify();
void verify_task(void *pvParameters);
void ota_task(void *pvParameters);
bool version_checker(const char *url);

} // namespace ota