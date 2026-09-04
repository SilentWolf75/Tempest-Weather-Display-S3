#pragma once
#include <lvgl.h>

// High-end procedural animated weather icon widget.
// Renders smooth vector animations (pulsing sun, drifting clouds, falling rain, flashing lightning).

lv_obj_t* weather_anim_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
void      weather_anim_set_icon(lv_obj_t *obj, const char *icon_slug);
void      weather_anim_stop(lv_obj_t *obj);
void      weather_anim_start(lv_obj_t *obj);
