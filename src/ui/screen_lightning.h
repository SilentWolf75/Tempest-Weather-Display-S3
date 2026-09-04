#pragma once
#include <lvgl.h>
#include "tempest_state.h"

lv_obj_t* screen_lightning_create(lv_obj_t *parent);
void      screen_lightning_update(const TempestState &state);
