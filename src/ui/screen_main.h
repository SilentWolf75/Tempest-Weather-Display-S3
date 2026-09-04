#pragma once
#include <lvgl.h>
#include "tempest_state.h"

lv_obj_t* screen_main_create(lv_obj_t *parent);
void      screen_main_update(const TempestState &state);
