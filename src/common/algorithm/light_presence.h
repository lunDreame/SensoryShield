#pragma once
#include "definition.h"
#include "common/algorithm/stimulus_policy.h"
#include "common/algorithm/recovery_guard.h"
struct LightTarget { bool on; uint8_t brightnessPercent; uint16_t cctMireds; };
class LightPresenceController final {
  public:
    static constexpr float EnterScore = StimulusPolicy::EnterScore, RecoverScore = StimulusPolicy::RecoverScore;
    static constexpr uint32_t EnterMs = StimulusPolicy::EnterMs, MinimumHoldMs = AUTO_RELIEF_HOLD_MS,
        RecoverMs = StimulusPolicy::RecoverMs, RecoveryStepMs = StimulusPolicy::RecoveryStepMs;
    void Reset() { *this = {}; }
    void OnDefaultOutputChanged() {
        // A failed recovery under the previous default must not lock out a
        // deliberate new default. Keep relief and its minimum hold intact.
        mRetry.Reset(); mHighPending = mLowPending = false;
        if (mState == State::Recover) {
            mRecoverPaused = true;
            mRecoveryStart = mLastSafe = mBrightness; mRecoveryStartCct = mCct;
        }
    }
    bool IsNormal() const { return mState == State::Normal; }
    LightTarget Evaluate(const SensorSnapshot& snapshot, const AppConfig& config, float score, uint32_t now = 0);
  private:
    enum class State { Normal, Relief, Recover };
    State mState = State::Normal;
    RecoveryGuard mRetry;
    bool mHighPending = false, mLowPending = false, mRecoverPaused = false;
    uint32_t mHighSince = 0, mLowSince = 0, mReliefSince = 0, mStepAt = 0;
    uint8_t mBrightness = 100, mRecoveryStart = 100, mLastSafe = 100, mPendingBrightness = 100;
    uint16_t mCct = 250, mRecoveryStartCct = 250;
};
