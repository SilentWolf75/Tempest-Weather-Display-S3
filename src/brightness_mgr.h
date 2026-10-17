#pragma once
#include <stdint.h>
#include "settings_mgr.h"
void brightness_preview(uint8_t level);
void brightness_cancel_preview();
void brightness_tick(const AppSettings &settings, uint32_t idle_ms);
