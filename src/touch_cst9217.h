#pragma once
#include <stdint.h>

// Minimal Arduino driver for CST9217 capacitive touch controller over I2C.
bool touch_begin();
bool touch_read(uint16_t *x, uint16_t *y);
int  touch_read_points(uint16_t x[2], uint16_t y[2]);
