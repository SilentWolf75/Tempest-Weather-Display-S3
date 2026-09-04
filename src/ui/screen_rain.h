#pragma once
#include <lvgl.h>
#include "tempest_state.h"

lv_obj_t* screen_rain_create(lv_obj_t *parent);
void      screen_rain_update(const TempestState &state);
