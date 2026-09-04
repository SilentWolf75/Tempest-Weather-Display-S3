#include "ui.h"
#include "screen_main.h"
#include "screen_wind.h"
#include "screen_lightning.h"
#include "screen_info.h"
#include "tempest_state.h"
#include "config.h"
#include <lvgl.h>
#include <Arduino.h>

static lv_obj_t *s_tv = nullptr;
static lv_obj_t *s_tile0 = nullptr;
static lv_obj_t *s_tile1 = nullptr;
static lv_obj_t *s_tile2 = nullptr;
static lv_obj_t *s_tile3 = nullptr;

static lv_obj_t *s_dot[4] = { nullptr, nullptr, nullptr, nullptr };
static int s_active_page = 0;

static void update_dots(int active) {
    s_active_page = active;
    for (int i = 0; i < 4; ++i) {
        if (!s_dot[i]) continue;
        if (i == active) {
            lv_obj_set_style_bg_color(s_dot[i], lv_color_hex(0x38BDF8), 0);
            lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
            lv_obj_set_size(s_dot[i], 18, 6);
        } else {
            lv_obj_set_style_bg_color(s_dot[i], lv_color_hex(0x64748B), 0);
            lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_60, 0);
            lv_obj_set_size(s_dot[i], 6, 6);
        }
    }
}

static void tv_scroll_event_cb(lv_event_t *e) {
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *act = lv_tileview_get_tile_act(tv);
    if (act == s_tile0) update_dots(0);
    else if (act == s_tile1) update_dots(1);
    else if (act == s_tile2) update_dots(2);
    else if (act == s_tile3) update_dots(3);
}

void ui_init() {
    Serial.println("[ui] Initializing UI tileview and 4 screens...");

    s_tv = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(s_tv, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_tv, lv_color_black(), 0);
    lv_obj_clear_flag(s_tv, LV_OBJ_FLAG_SCROLL_ELASTIC);

    // 4 Horizontal Tiles: Main (0,0), Wind (1,0), Lightning (2,0), Info (3,0)
    s_tile0 = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
    s_tile1 = lv_tileview_add_tile(s_tv, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    s_tile2 = lv_tileview_add_tile(s_tv, 2, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    s_tile3 = lv_tileview_add_tile(s_tv, 3, 0, LV_DIR_LEFT);

    screen_main_create(s_tile0);
    screen_wind_create(s_tile1);
    screen_lightning_create(s_tile2);
    screen_info_create(s_tile3);

    lv_obj_add_event_cb(s_tv, tv_scroll_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Page indicator dots pinned on top layer
    lv_obj_t *top_layer = lv_layer_top();
    const int dot_x[4] = { -30, -10, 10, 30 };
    for (int i = 0; i < 4; ++i) {
        s_dot[i] = lv_obj_create(top_layer);
        lv_obj_remove_style_all(s_dot[i]);
        lv_obj_set_size(s_dot[i], 6, 6);
        lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_align(s_dot[i], LV_ALIGN_BOTTOM_MID, dot_x[i], -18);
        lv_obj_clear_flag(s_dot[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    update_dots(0);

    Serial.println("[ui] UI initialized successfully with 4 screens");
}

void ui_update() {
    TempestState state;
    tempest_get_state(&state);

    // Always update all screens so when swiped to, they are current
    screen_main_update(state);
    screen_wind_update(state);
    screen_lightning_update(state);
    screen_info_update(state);
}

void ui_set_screen(int idx) {
    if (!s_tv) return;
    if (idx < 0) idx = 0;
    if (idx > 3) idx = 3;
    lv_obj_set_tile_id(s_tv, idx, 0, LV_ANIM_ON);
    update_dots(idx);
}

void ui_next_screen() {
    if (!s_tv) return;
    // Don't auto-scroll while viewing the Info screen (screen 3)
    if (s_active_page >= 3) return;

    // Cycle only between the 3 main weather screens (0: Main, 1: Wind, 2: Lightning)
    int next_page = (s_active_page + 1) % 3;
    ui_set_screen(next_page);
}

int ui_get_active_screen() {
    return s_active_page;
}
