#pragma once
#include <stdint.h>

void ui_init();
void ui_update();
void ui_next_screen();
void ui_set_screen(int idx);
int  ui_get_active_screen();
void ui_pause_autoscroll(uint32_t ms);
