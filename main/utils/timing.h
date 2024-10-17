#pragma once

#include "freertos/FreeRTOS.h"

void delay(const unsigned int duration_ms);

unsigned long int micros();
unsigned long int millis();

unsigned long millis_since(const unsigned long time);
unsigned long micros_since(const unsigned long time);