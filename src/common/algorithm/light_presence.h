#pragma once

#include "definition.h"

struct LightTarget {
    bool on;
    uint8_t brightnessPercent;
    uint16_t cctMireds;
};

// Board-independent lighting policy. Sensor collection and physical output stay in System/Peripheral.
class LightPresenceController final {
  public:
    LightTarget Evaluate(const SensorSnapshot& snapshot, const AppConfig& config, float sensoryScore) const;
};
