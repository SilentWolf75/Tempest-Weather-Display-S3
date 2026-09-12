#pragma once
// Waveshare ESP32-P4-WIFI6-Touch-LCD-4C — 4" round IPS, 720x720, MIPI-DSI.
//
// Hardware:
//   ESP32-P4NRW32 dual-core RISC-V (HP) + 40 MHz RISC-V (LP)
//   32 MB PSRAM, 32 MB NOR flash over QSPI
//   4" round IPS 720x720, MIPI-DSI 2-lane
//   ESP32-C6-MINI-1 Wi-Fi 6 co-processor over SDIO (esp_hosted)
//   GT9271 capacitive touch (I2C)

#define BOARD_HOSTNAME      "weather-p4"
#define BOARD_SETUP_AP      "Weather-Display-Setup"
#define BOARD_NAME          "Waveshare ESP32-P4-WIFI6-Touch-LCD-4C"
#define BOARD_CHIP_NAME     "ESP32-P4"
#define BOARD_PANEL_QSPI    0
#define BOARD_PANEL_DSI     1

// ---------- Screen Geometry ----------
#define SCREEN_W            720
#define SCREEN_H            720
#define SCREEN_CX           360
#define SCREEN_CY           360
#define LCD_COL_OFFSET      0
#define LCD_ROW_OFFSET      0

// ---------- Shared I2C ----------
#define PIN_I2C_SDA         7
#define PIN_I2C_SCL         8

// ---------- Panel / Touch ----------
#define PIN_LCD_RST         27
#define PIN_LCD_BL          26
#define BRIGHTNESS_IDLE     64
#define BACKLIGHT_MIN_ON    64
#define PIN_TP_INT          -1
#define PIN_TP_RST          23
#define TP_MIRROR_X         false
#define TP_MIRROR_Y         false
#define TP_SWAP_XY          false
#define I2C_ADDR_TOUCH      0x5D
#define I2C_ADDR_TOUCH_ALT  0x14
#define I2C_CLOCK_HZ        100000

// ---------- MIPI-DSI Timings ----------
#define DSI_LANES           2
#define DSI_LANE_BITRATE_HZ 1500000000UL
#define DSI_PCLK_HZ         80000000UL
#define DSI_HSYNC_PULSE     20
#define DSI_HSYNC_FRONT     40
#define DSI_HSYNC_BACK      20
#define DSI_VSYNC_PULSE     4
#define DSI_VSYNC_FRONT     24
#define DSI_VSYNC_BACK      12
#define DSI_LDO_CHANNEL     3
#define DSI_LDO_MV          2500

// ---------- Peripherals Present ----------
#define BOARD_HAS_WIFIMANAGER 0
#define BOARD_WIFI_HOSTED     1
#define LVGL_BUF_LINES        40
