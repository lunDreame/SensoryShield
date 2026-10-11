#pragma once

#include "definition.h"

// Nominal endpoints of the existing WS2812B white blend, independent of user limits.
// Actual emitted colour temperature still requires hardware calibration.
inline uint8_t WhiteToneCoolPercent(uint16_t mireds) {
    constexpr uint16_t minMireds = 250;
    constexpr uint16_t maxMireds = 454;
    const uint16_t clamped = ClampValue<uint16_t>(mireds, minMireds, maxMireds);
    return static_cast<uint8_t>(100U - ((clamped - minMireds) * 100U) / (maxMireds - minMireds));
}
