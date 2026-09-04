#pragma once
#include <lvgl.h>
#include "tempest_state.h"

lv_obj_t* screen_wind_create(lv_obj_t *parent);
void      screen_wind_update(const TempestState &state);
