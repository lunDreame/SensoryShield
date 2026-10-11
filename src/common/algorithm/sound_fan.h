#pragma once
#include "common/algorithm/stimulus_score.h"
#include "common/algorithm/stimulus_policy.h"
#include "common/algorithm/recovery_guard.h"
#include "definition.h"
#include <math.h>

class SoundFanController final {
  public:
    static constexpr uint32_t StaleMs = 1000, EnterQuietMs = StimulusPolicy::EnterMs,
        RecoverMs = StimulusPolicy::RecoverMs;
    static constexpr uint8_t NormalPercent = AUTO_FAN_NORMAL_PERCENT, QuietPercent = AUTO_FAN_QUIET_PERCENT;
    void Reset() { *this = {}; }
    bool IsNormal() const { return mState == State::Normal; }
    uint8_t Evaluate(const SensorSnapshot& snapshot, const AppConfig& config,
                     const StimulusScore& stimulus, uint32_t now) {
        const uint8_t cap = config.fanMaxPercent > 100 ? 100 : config.fanMaxPercent;
        const uint8_t normal = ClampValue<uint8_t>(config.preferredFanPercent, 0, cap);
        const uint8_t quiet = normal < config.reliefFanPercent ? normal : config.reliefFanPercent;
        if ((config.occupancyAutomationEnabled && (!snapshot.pirValid || !snapshot.occupied)) || normal == 0U) { Reset(); return 0; }
        if (mPercent > normal) mPercent = normal;
        const auto& sound = snapshot.sound;
        const bool valid = snapshot.micValid && sound.valid && isfinite(stimulus.sound) && stimulus.sound >= 0 &&
            isfinite(sound.energy) && isfinite(sound.peak) && isfinite(sound.delta) &&
            sound.energy >= 0 && sound.energy <= 1 && sound.peak >= sound.energy && sound.peak <= 1 &&
            sound.delta >= 0 && sound.delta <= 1 && now - sound.timestampMs <= StaleMs;
        if (!valid) {
            mHighPending = mLowPending = false; mRecoverPaused = true;
            if (mPercent > quiet) mPercent = quiet;
            mLastUpdate = now;
            return mPercent;
        }
        if (mSeen && sound.timestampMs == mLastSample) return mPercent;
        if (mSeen && sound.timestampMs - mLastSample > StaleMs) {
            mHighPending = mLowPending = false; mRecoverPaused = true;
        }
        mLastSample = sound.timestampMs; mSeen = true;
        // Start at ordinary output so the initial baseline represents normal use.
        // Initial valid input is not proof of comfort, but must not deadlock learning at quiet output.
        if (!mInitialized) { mPercent = normal; mInitialized = true; mLastUpdate = now; }
        const bool settled = snapshot.controlInputsSettled;
        // The first admitted sample already supplies a provisional comparison.
        // Window readiness is diagnostic; it must not suppress a real early increase.
        const bool high = stimulus.sound >= StimulusPolicy::EnterScore;
        const bool low = stimulus.sound <= StimulusPolicy::RecoverScore;
        if (!settled) {
            mHighPending = mLowPending = false;
            mLastUpdate = now;
            return mPercent;
        }
        if (mState == State::Normal) {
            mLowPending = false;
            if (high) {
                if (!mHighPending) { mHighPending = true; mPendingSince = sound.timestampMs; }
                if (sound.timestampMs - mPendingSince >= EnterQuietMs) {
                    mState = State::Relief; mReliefSince = sound.timestampMs; mHighPending = false;
                }
            } else mHighPending = false;
        } else if (mState == State::Relief) {
            mHighPending = false;
            const bool atQuiet = mPercent <= quiet || mRetry.IsBlocked();
            const bool canRetry = atQuiet && mRetry.MayRecover(snapshot, false);
            if (canRetry && low && sound.timestampMs - mReliefSince >= config.reliefHoldMs) {
                if (!mLowPending) { mLowPending = true; mPendingSince = sound.timestampMs; }
                if (sound.timestampMs - mPendingSince >= config.recoverConfirmMs) {
                    mState = State::Recover; mLowPending = false; mRecoverPaused = false;
                    mLastSafe = mPercent; mLastUpdate = now;
                }
            } else mLowPending = false;
        } else {
            if (high) {
                mLowPending = false; mRecoverPaused = true; mLastUpdate = now;
                if (!mHighPending) { mHighPending = true; mPendingSince = sound.timestampMs; }
                if (sound.timestampMs - mPendingSince >= EnterQuietMs) {
                    mState = State::Relief; mReliefSince = sound.timestampMs; mHighPending = false;
                    if (mPercent > mLastSafe) mPercent = mLastSafe;
                    mRetry.Block();
                }
            } else if (!low) {
                mHighPending = mLowPending = false; mRecoverPaused = true; mLastUpdate = now;
            } else {
                mHighPending = false;
                if (mRecoverPaused) {
                    if (!mLowPending) { mLowPending = true; mPendingSince = sound.timestampMs; }
                    if (sound.timestampMs - mPendingSince < config.recoverConfirmMs) return mPercent;
                    mLowPending = false; mRecoverPaused = false; mLastUpdate = now;
                }
                if (now - mLastUpdate >= StimulusPolicy::RecoveryStepMs) {
                    mLastSafe = mPercent;
                    const uint8_t remaining = normal - mPercent;
                    mPercent += remaining > StimulusPolicy::OutputStepPercent ? StimulusPolicy::OutputStepPercent : remaining;
                    mLastUpdate = now;
                    if (mPercent == normal && (!snapshot.observedOutputs.valid ||
                        snapshot.observedOutputs.fanPercent == normal)) {
                        mState = State::Normal; mRetry.Reset();
                    }
                }
            }
        }
        if (mState != State::Recover && now - mLastUpdate >= ALGORITHM_PERIOD_MS) {
            const uint8_t desired = mState == State::Relief ?
                (mRetry.IsBlocked() && !high ? mPercent : quiet) : normal;
            if (mPercent < desired) mPercent += desired - mPercent > 2 ? 2 : desired - mPercent;
            if (mPercent > desired) mPercent -= mPercent - desired > 2 ? 2 : mPercent - desired;
            mLastUpdate = now;
        }
        return mPercent;
    }
  private:
    enum class State { Normal, Relief, Recover };
    State mState = State::Normal;
    RecoveryGuard mRetry;
    bool mSeen = false, mHighPending = false, mLowPending = false, mInitialized = false, mRecoverPaused = false;
    uint8_t mPercent = 0, mLastSafe = 0;
    uint32_t mLastSample = 0, mLastUpdate = 0, mPendingSince = 0, mReliefSince = 0;
};
