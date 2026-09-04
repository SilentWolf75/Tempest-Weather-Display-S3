#pragma once
#include <lvgl.h>
#include "tempest_state.h"

lv_obj_t* screen_info_create(lv_obj_t *parent);
void      screen_info_update(const TempestState &state);
