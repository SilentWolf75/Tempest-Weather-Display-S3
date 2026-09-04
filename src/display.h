#pragma once
#include <stdint.h>

namespace display {

bool begin();
void loop();
void setBrightness(uint8_t v);
uint32_t inactiveMs();
void setRotation(uint16_t degrees);
uint16_t rotation();
const uint16_t* captureFrame();

} // namespace display
