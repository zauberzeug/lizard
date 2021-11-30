#pragma once

#include "freertos/FreeRTOS.h"

void delay(unsigned int duration_ms);

unsigned long int IRAM_ATTR micros();
unsigned long int IRAM_ATTR millis();

unsigned long millis_since(unsigned long time);
unsigned long micros_since(unsigned long time);