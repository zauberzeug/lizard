#pragma once

#include "freertos/FreeRTOS.h"

void delay(const unsigned int duration_ms);

unsigned long int IRAM_ATTR micros();
unsigned long int IRAM_ATTR millis();

unsigned long millis_since(const unsigned long time);
unsigned long micros_since(const unsigned long time);