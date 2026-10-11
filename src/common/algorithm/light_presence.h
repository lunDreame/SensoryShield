#pragma once
#include "definition.h"
struct LightTarget { bool on; uint8_t brightnessPercent; uint16_t cctMireds; };
class LightPresenceController final {
  public:
    // Initial engineering settings; validate with simulation and the actual installation.
    static constexpr float EnterScore = 1.4f, RecoverScore = 0.7f;
    static constexpr uint32_t EnterMs = 750, MinimumHoldMs = 1500, RecoverMs = 2500, RecoveryStepMs = 500;
    void Reset() { *this = {}; }
    LightTarget Evaluate(const SensorSnapshot& snapshot, const AppConfig& config, float score, uint32_t now = 0);
  private:
    enum class State { Normal, Relief, Recover };
    State mState = State::Normal;
    bool mHighPending = false, mLowPending = false;
    uint32_t mHighSince = 0, mLowSince = 0, mReliefSince = 0, mStepAt = 0;
    uint8_t mBrightness = 100;
};
