#include "common/algorithm/light_presence.h"

LightTarget LightPresenceController::Evaluate(const SensorSnapshot& snapshot, const AppConfig& config,
                                              float sensoryScore) const {
    if (!snapshot.occupied) {
        return {false, 0, config.maxCCTMireds};
    }

    const float limitedScore = ClampValue(sensoryScore, 0.0f, 8.0f);
    const float normalized = limitedScore / 8.0f;
    const uint8_t brightnessRange = config.maxBrightness - config.minBrightness;
    const uint8_t brightness =
        ClampValue<uint8_t>(static_cast<uint8_t>(config.maxBrightness - (brightnessRange * normalized)),
                            config.minBrightness, config.maxBrightness);

    const uint16_t cctRange = config.maxCCTMireds - config.minCCTMireds;
    const uint16_t cct =
        ClampValue<uint16_t>(static_cast<uint16_t>(config.minCCTMireds + (cctRange * normalized)),
                             config.minCCTMireds, config.maxCCTMireds);

    return {true, brightness, cct};
}
