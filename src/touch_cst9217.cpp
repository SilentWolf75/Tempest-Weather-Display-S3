#include "touch_cst9217.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>

#define CST9217_REG_DATA  0xD000
#define CST9217_ACK       0xAB
#define CST9217_DATA_LEN  10
#define CST9217_DATA_LEN2 15

#ifndef TP_MIRROR_X
#define TP_MIRROR_X true
#endif
#ifndef TP_MIRROR_Y
#define TP_MIRROR_Y true
#endif

static bool cst_read_reg(uint16_t reg, uint8_t *data, uint8_t len) {
    Wire.beginTransmission((uint8_t)I2C_ADDR_TOUCH);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(true) != 0) return false;
    delayMicroseconds(500);
    if (Wire.requestFrom((uint8_t)I2C_ADDR_TOUCH, len) < len) return false;
    for (uint8_t i = 0; i < len; ++i) data[i] = Wire.read();
    return true;
}

bool touch_begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW);
    delay(10);
    digitalWrite(PIN_TP_RST, HIGH);
    delay(50);
    pinMode(PIN_TP_INT, INPUT);

    uint8_t d[CST9217_DATA_LEN] = {0};
    if (cst_read_reg(CST9217_REG_DATA, d, CST9217_DATA_LEN) && d[6] == CST9217_ACK) {
        Serial.println("[touch] CST9217 responding (ACK ok)");
    } else {
        Serial.println("[touch] CST9217 not responding yet (will keep polling)");
    }
    return true;
}

static bool cst_unpack_point(const uint8_t *b, uint16_t *ox, uint16_t *oy) {
    if ((b[0] & 0x0F) != 0x06) return false;
    uint16_t x = ((uint16_t)b[1] << 4) | (b[3] >> 4);
    uint16_t y = ((uint16_t)b[2] << 4) | (b[3] & 0x0F);
    if (x > SCREEN_W - 1) x = SCREEN_W - 1;
    if (y > SCREEN_H - 1) y = SCREEN_H - 1;
    if (TP_MIRROR_X) x = (SCREEN_W - 1) - x;
    if (TP_MIRROR_Y) y = (SCREEN_H - 1) - y;
    *ox = x;
    *oy = y;
    return true;
}

bool touch_read(uint16_t *ox, uint16_t *oy) {
    uint8_t d[CST9217_DATA_LEN];
    if (!cst_read_reg(CST9217_REG_DATA, d, CST9217_DATA_LEN)) return false;
    if (d[6] != CST9217_ACK) return false;

    const uint8_t points = d[5] & 0x7F;
    if (points == 0) return false;
    return cst_unpack_point(d, ox, oy);
}

int touch_read_points(uint16_t x[2], uint16_t y[2]) {
    uint8_t d[CST9217_DATA_LEN2];
    if (!cst_read_reg(CST9217_REG_DATA, d, CST9217_DATA_LEN2)) return 0;
    if (d[6] != CST9217_ACK) return 0;

    const uint8_t points = d[5] & 0x7F;
    if (points == 0) return 0;

    int n = 0;
    if (cst_unpack_point(d, &x[n], &y[n])) ++n;
    if (points >= 2 && cst_unpack_point(d + 7, &x[n], &y[n])) ++n;
    return n;
}
