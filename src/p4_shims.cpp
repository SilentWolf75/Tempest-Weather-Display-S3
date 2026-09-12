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
#include "touch_gt911.h"
#include <esp_heap_caps.h>

namespace display {

static lv_disp_draw_buf_t s_dbuf;
static lv_disp_drv_t      s_ddrv;
static lv_indev_drv_t     s_idrv;
static lv_color_t        *s_buf = nullptr;

bool begin() {
    if (!display_p4_begin()) {
        Serial.println("[p4] DSI bring-up failed");
        return false;
    }

    lv_init();
    const size_t px = (size_t)SCREEN_W * LVGL_BUF_LINES;
    // Internal DMA RAM preferred for fast 2D blitting
    s_buf = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_buf) {
        Serial.println("[p4] internal draw buffer failed; falling back to PSRAM");
        s_buf = (lv_color_t *)heap_caps_malloc(px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_buf) {
        Serial.println("[p4] draw buffer alloc failed");
        return false;
    }
    lv_disp_draw_buf_init(&s_dbuf, s_buf, nullptr, px);

    lv_disp_drv_init(&s_ddrv);
    s_ddrv.hor_res  = SCREEN_W;
    s_ddrv.ver_res  = SCREEN_H;
    s_ddrv.flush_cb = display_p4_flush;
    s_ddrv.draw_buf = &s_dbuf;
    lv_disp_drv_register(&s_ddrv);

    if (touch_gt911_begin()) {
        lv_indev_drv_init(&s_idrv);
        s_idrv.type    = LV_INDEV_TYPE_POINTER;
        s_idrv.read_cb = touch_gt911_lvgl_read;
        lv_indev_drv_register(&s_idrv);
        Serial.println("[p4] GT9271 touch registered with LVGL");
    }

    Serial.printf("[p4] PSRAM free: %u KB, Heap free: %u KB\n",
                  (unsigned)(ESP.getFreePsram() / 1024),
                  (unsigned)(ESP.getFreeHeap() / 1024));
    return true;
}

void loop() {
    lv_timer_handler();
}

void setBrightness(uint8_t v) {
    if (v > 0 && v < BACKLIGHT_MIN_ON) v = BACKLIGHT_MIN_ON;
    display_p4_backlight_level(v);
}

void setRotation(uint16_t) {}
uint16_t rotation() { return 0; }

const uint16_t *captureFrame() {
    return display_p4_framebuffer();
}

uint32_t inactiveMs() {
    return lv_disp_get_inactive_time(nullptr);
}

} // namespace display

#endif // BOARD_WAVESHARE_P4_LCD_4C
