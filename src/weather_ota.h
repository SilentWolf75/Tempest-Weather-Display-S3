#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Image layout: esp_image_header_t (24) + segment header (8) + esp_app_desc_t.
// chip_id is at byte 12. App-desc magic is at byte 32. project_name is at byte 80.
inline bool ota_image_acceptable(const uint8_t *image, size_t length, uint16_t chip, const char *project) {
    if (!image || length < 112 || !project || !project[0]) return false;
    if (image[0] != 0xE9) return false;
    uint16_t image_chip = (uint16_t)image[12] | ((uint16_t)image[13] << 8);
    if (image_chip != chip) return false;
    uint32_t magic = (uint32_t)image[32] | ((uint32_t)image[33] << 8) |
                     ((uint32_t)image[34] << 16) | ((uint32_t)image[35] << 24);
    if (magic != 0xABCD5432u) return false;
    return strncmp((const char *)image + 80, project, 32) == 0;
}

// Present in this firmware, so an uploaded image has to be this project.
inline const char *ota_image_marker() { return "tempest-weather-display-v1"; }

inline bool ota_note_marker(uint8_t *tail, size_t *tail_len, const uint8_t *data, size_t len) {
    const char *marker = ota_image_marker();
    const size_t mlen = strlen(marker);
    if (!data || !len) return false;
    if (*tail_len) {
        uint8_t bridge[64];
        size_t n = *tail_len;
        if (n > sizeof(bridge)) n = sizeof(bridge);
        memcpy(bridge, tail, n);
        size_t take = len < (mlen - 1) ? len : (mlen - 1);
        if (n + take > sizeof(bridge)) take = sizeof(bridge) - n;
        memcpy(bridge + n, data, take);
        for (size_t i = 0; i + mlen <= n + take; ++i) {
            if (memcmp(bridge + i, marker, mlen) == 0) return true;
        }
    }
    if (len >= mlen) {
        for (size_t i = 0; i + mlen <= len; ++i) {
            if (memcmp(data + i, marker, mlen) == 0) return true;
        }
    }
    size_t keep = len < (mlen - 1) ? len : (mlen - 1);
    memcpy(tail, data + len - keep, keep);
    *tail_len = keep;
    return false;
}

#ifdef ESP_PLATFORM
#include <esp_app_format.h>
#include <esp_ota_ops.h>
inline const char *ota_expected_project() { return esp_app_get_description()->project_name; }
inline uint16_t ota_expected_chip() {
#if defined(BOARD_WAVESHARE_P4_LCD_4C)
    return 18; // ESP_CHIP_ID_ESP32P4
#else
    return 9;  // ESP_CHIP_ID_ESP32S3
#endif
}
#else
inline const char *ota_expected_project() { return "arduino-lib-builder"; }
inline uint16_t ota_expected_chip() { return 9; }
#endif
