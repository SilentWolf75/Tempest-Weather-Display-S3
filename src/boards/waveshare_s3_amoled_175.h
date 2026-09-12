#pragma once
// Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// ESP32-S3R8, 8 MB PSRAM, 16 MB flash. CO5300 AMOLED 466x466 over QSPI, CST9217 touch.

#define BOARD_HOSTNAME      "weather-s3"
#define BOARD_SETUP_AP      "Weather-Display-Setup"
#define BOARD_NAME          "Waveshare ESP32-S3-Touch-AMOLED-1.75"
#define BOARD_CHIP_NAME     "ESP32-S3"
#define BOARD_PANEL_QSPI    1
#define BOARD_PANEL_DSI     0

// ---------- Screen Geometry ----------
#define SCREEN_W            466
#define SCREEN_H            466
#define SCREEN_CX           233
#define SCREEN_CY           233
#define LCD_COL_OFFSET      6
#define LCD_ROW_OFFSET      0
#define LCD_QSPI_HZ         80000000

// ---------- Display & Touch Pins ----------
#define PIN_LCD_CS          12
#define PIN_LCD_SCLK        38
#define PIN_LCD_D0          4
#define PIN_LCD_D1          5
#define PIN_LCD_D2          6
#define PIN_LCD_D3          7
#define PIN_LCD_RST         39

#define PIN_I2C_SDA         15
#define PIN_I2C_SCL         14
#define PIN_TP_INT          11
#define PIN_TP_RST          40
#define I2C_ADDR_TOUCH      0x5A    // CST9217 capacitive touch
#define TP_MIRROR_X         true
#define TP_MIRROR_Y         true

// ---------- Peripherals Present ----------
#define BOARD_HAS_WIFIMANAGER 1
#define BOARD_WIFI_HOSTED     0
#define LVGL_BUF_LINES        32
