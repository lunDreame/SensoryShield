#pragma once
#include "definition.h"
#include "common/algorithm/stimulus_policy.h"
#include "common/algorithm/recovery_guard.h"

struct SensoryTarget { bool on; uint8_t brightnessPercent; uint16_t cctMireds; uint8_t fanPercent; };

// One multisensory episode owns confirmation, hold, recovery and retry for both outputs.
class SensoryReliefController final {
  public:
    static constexpr float EnterScore = StimulusPolicy::EnterScore, RecoverScore = StimulusPolicy::RecoverScore;
    static constexpr uint32_t EnterMs = StimulusPolicy::EnterMs, RecoveryStepMs = StimulusPolicy::RecoveryStepMs;
    void Reset() { *this = {}; }
    void OnDefaultOutputChanged() {
        mRetry.Reset(); mHighPending = mLowPending = false;
        if (mState == State::Recover) {
            mRecoverPaused = true;
            mRecoveryStart = mLastSafe = mBrightness;
            mRecoveryFan = mLastSafeFan = mFan;
            mRecoveryStartCct = mCct;
        }
    }
    bool IsNormal() const { return mState == State::Normal; }
    SensoryTarget Evaluate(const SensorSnapshot&, const AppConfig&, float combinedScore, uint32_t now);
  private:
    enum class State { Normal, Relief, Recover };
    State mState = State::Normal;
    RecoveryGuard mRetry;
    bool mSeen = false, mHighPending = false, mLowPending = false, mRecoverPaused = false;
    uint32_t mLastSample = 0, mHighSince = 0, mLowSince = 0, mReliefSince = 0, mStepAt = 0, mFanStepAt = 0;
    uint8_t mBrightness = 100, mRecoveryStart = 100, mLastSafe = 100, mPendingBrightness = 100;
    uint8_t mFan = 0, mRecoveryFan = 0, mLastSafeFan = 0;
    uint16_t mCct = 250, mRecoveryStartCct = 250;
    SensoryTarget Target() const { return {mBrightness > 0, mBrightness, mCct, mFan}; }
};
