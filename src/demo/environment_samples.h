#pragma once
#include "common/algorithm/environment_measurement.h"

namespace RehearsalBaseline {
// Fill with 31 paired REAL lux / normalized PCM RMS samples at the venue.
// Sampling configuration: docs/control.md.
inline constexpr bool Enabled = false;
inline const InitialEnvironment Samples = {
    0,  // Set to 31 after filling BOTH arrays with actual samples.
    {}, // lux: 31 actual illuminance observations, in lx.
    {}, // sound: 31 paired normalized PCM RMS observations, in [0,1].
};
inline constexpr uint8_t Brightness = 100, Fan = 100;
inline constexpr uint16_t CctMireds = 250;
inline bool Matches(const AppConfig& c) {
    return ClampValue(c.preferredBrightness, c.minBrightness, c.maxBrightness) == Brightness &&
           ClampValue(c.normalCCTMireds, c.minCCTMireds, c.maxCCTMireds) == CctMireds &&
           NormalizeFanPercent(c.preferredFanPercent, c.fanMaxPercent) == Fan;
}
} // namespace RehearsalBaseline
