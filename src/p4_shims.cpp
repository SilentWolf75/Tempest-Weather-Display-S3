// Board shims for the ESP32-P4-WIFI6-Touch-LCD-4C.
//
// Implements the display:: namespace using display_p4 and touch_gt911.
#include "config.h"
#if defined(BOARD_WAVESHARE_P4_LCD_4C)

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>
#include "display.h"
#include "display_p4.h"
#include "settings_mgr.h"
#include "touch_gt911.h"
#include <esp_heap_caps.h>

namespace display {

static lv_disp_draw_buf_t s_dbuf;
static lv_disp_drv_t      s_ddrv;
static lv_indev_drv_t     s_idrv;
static lv_color_t        *s_buf = nullptr;
static lv_color_t        *s_buf2 = nullptr;
static lv_color_t        *s_rot_buf = nullptr;
static uint16_t           s_rot = 0;

static lv_color_t *alloc_draw(size_t bytes) {
    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) buf = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    return buf;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    int16_t w = (int16_t)(area->x2 - area->x1 + 1);
    int16_t h = (int16_t)(area->y2 - area->y1 + 1);
    lv_color_t *out = px;
    int16_t dx = area->x1, dy = area->y1;
    uint16_t dw = (uint16_t)w, dh = (uint16_t)h;
    switch (s_rot) {
        case 180:
            for (int i = 0, j = w * h - 1; i < j; ++i, --j) {
                lv_color_t t = px[i]; px[i] = px[j]; px[j] = t;
            }
            dx = (int16_t)(SCREEN_W - 1 - area->x2);
            dy = (int16_t)(SCREEN_H - 1 - area->y2);
            break;
        case 90:
            if (s_rot_buf) {
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        s_rot_buf[i * h + (h - 1 - j)] = px[j * w + i];
                out = s_rot_buf; dw = (uint16_t)h; dh = (uint16_t)w;
                dx = (int16_t)(SCREEN_H - 1 - area->y2); dy = area->x1;
            }
            break;
        case 270:
            if (s_rot_buf) {
                for (int j = 0; j < h; ++j)
                    for (int i = 0; i < w; ++i)
                        s_rot_buf[(w - 1 - i) * h + j] = px[j * w + i];
                out = s_rot_buf; dw = (uint16_t)h; dh = (uint16_t)w;
                dx = area->y1; dy = (int16_t)(SCREEN_W - 1 - area->x2);
            }
            break;
        default: break;
    }
    display_p4_blit(dx, dy, dw, dh, out);
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *data) {
    static int last_x = 0, last_y = 0;
    int x = last_x, y = last_y;
    if (touch_gt911_read(&x, &y)) {
        int lx = x, ly = y;
        switch (s_rot) {
            case 90:  lx = y;                ly = SCREEN_H - 1 - x; break;
            case 180: lx = SCREEN_W - 1 - x; ly = SCREEN_H - 1 - y; break;
            case 270: lx = SCREEN_W - 1 - y; ly = x; break;
            default: break;
        }
        last_x = lx; last_y = ly;
        data->point.x = (lv_coord_t)lx;
        data->point.y = (lv_coord_t)ly;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->point.x = (lv_coord_t)last_x;
        data->point.y = (lv_coord_t)last_y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool begin() {
    if (!display_p4_begin()) {
        Serial.println("[p4] DSI bring-up failed");
        return false;
    }

    lv_init();
    const size_t bytes = (size_t)SCREEN_W * LVGL_BUF_LINES * sizeof(lv_color_t);
    s_buf = alloc_draw(bytes);
    if (!s_buf) {
        Serial.println("[p4] draw buffer alloc failed");
        return false;
    }
    s_buf2 = alloc_draw(bytes);
    if (!s_buf2) Serial.println("[p4] second draw buffer unavailable; using one buffer");
    s_rot_buf = (lv_color_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!s_rot_buf) Serial.println("[p4] rotation buffer unavailable");
    lv_disp_draw_buf_init(&s_dbuf, s_buf, s_buf2, (size_t)SCREEN_W * LVGL_BUF_LINES);

    lv_disp_drv_init(&s_ddrv);
    s_ddrv.hor_res  = SCREEN_W;
    s_ddrv.ver_res  = SCREEN_H;
    s_ddrv.flush_cb = flush_cb;
    s_ddrv.draw_buf = &s_dbuf;
    lv_disp_drv_register(&s_ddrv);

    AppSettings settings;
    settings_get(&settings);
    s_rot = settings.screen_rotation;

    if (touch_gt911_begin()) {
        lv_indev_drv_init(&s_idrv);
        s_idrv.type    = LV_INDEV_TYPE_POINTER;
        s_idrv.read_cb = touch_read_cb;
        lv_indev_drv_register(&s_idrv);
        Serial.println("[p4] GT9271 touch registered with LVGL");
    }

    Serial.printf("[p4] PSRAM free: %u KB, Heap free: %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024),
                  (unsigned)(ESP.getFreeHeap() / 1024));
    setBrightness(settings.brightness_day);
    return true;
}

void loop() {
    lv_timer_handler();
}

void setBrightness(uint8_t v) {
    if (v > 0 && v < BACKLIGHT_MIN_ON) v = BACKLIGHT_MIN_ON;
    display_p4_backlight_level(v);
}

void setRotation(uint16_t degrees) {
    s_rot = (uint16_t)(degrees % 360);
    lv_obj_t *scr = lv_scr_act();
    if (scr) lv_obj_invalidate(scr);
}
uint16_t rotation() { return s_rot; }

const uint16_t *captureFrame() {
    return display_p4_framebuffer();
}

uint32_t inactiveMs() {
    return lv_disp_get_inactive_time(nullptr);
}

} // namespace display

#endif // BOARD_WAVESHARE_P4_LCD_4C
