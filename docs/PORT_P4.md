# ESP32-P4 panel bring-up

The P4 firmware targets the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C: a 720×720 round MIPI-DSI panel, GT9271 touch, and an ESP32-C6 Wi-Fi coprocessor on SDIO.

Display init follows the ESP-IDF sequence in `src/display_p4.cpp`:

1. Pulse the panel reset pin.
2. Enable the internal LDO that powers the DSI PHY. Without it the bus reports success and the panel stays black.
3. Create the two-lane DSI bus, a DBI channel for the init registers, then the DPI video panel.
4. Hand LVGL frames to `esp_lcd_panel_draw_bitmap`.

Timings and the transcribed vendor init sequence live in `src/boards/waveshare_p4_lcd_4c.h` and `src/boards/p4_panel_init.h`.

LVGL uses two draw buffers when internal DMA or PSRAM can hold them. The panel scanout itself is not rotatable. Rotation of 0°, 90°, 180°, and 270° is applied in the flush callback, and touch coordinates are mapped to match. A missing rotation buffer leaves 90° and 270° unrotated rather than failing display init.

The module has 32 MB of flash. The partition table uses the first 16 MB, shared with the S3 layout. Pressure history and the rain ledger are stored in NVS, not in the unused upper flash, so both boards keep the same record across a reboot.
