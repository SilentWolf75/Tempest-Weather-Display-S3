#pragma once
#include <math.h>
#include <stdio.h>
#include <stddef.h>

inline void ui_number(char *out, size_t size, float value, int decimals = 0) {
    if (isfinite(value)) snprintf(out, size, "%.*f", decimals, (double)value);
    else snprintf(out, size, "--");
}
