// Minimal PC stand-in for the Arduino core, just enough to compile firmware/src/ui.cpp.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LOW 0
#define HIGH 1
#define OUTPUT 1
uint32_t millis();
void delay(uint32_t ms);
inline void digitalWrite(int, int) {}
inline void pinMode(int, int) {}
