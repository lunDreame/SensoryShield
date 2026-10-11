#include "common/algorithm/light_presence.h"
#include <math.h>

LightTarget LightPresenceController::Evaluate(const SensorSnapshot& snapshot, const AppConfig& config,
                                              float score, uint32_t now) {
    const uint8_t normal = ClampValue<uint8_t>(config.preferredBrightness, config.minBrightness, config.maxBrightness);
    const uint16_t normalCct = ClampValue<uint16_t>(config.normalCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    const uint16_t reliefCct = ClampValue<uint16_t>(config.preferredCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    if (config.occupancyAutomationEnabled && !snapshot.occupied) { Reset(); return {false, 0, normalCct}; }
    const bool trusted = snapshot.illuminanceValid && snapshot.micValid && snapshot.sound.valid && isfinite(score) &&
        (!snapshot.observedOutputs.valid || now - snapshot.observedAtMs <= 1000U);
    const bool valid = trusted && snapshot.controlInputsSettled;
    const float limited = isfinite(score) ? ClampValue(score, 0.0f, STIMULUS_MAX_SCORE) : STIMULUS_MAX_SCORE;
    // A threshold crossing starts with a small drop, rather than jumping to 65%.
    // Stronger scores interpolate to the configured lower bound; physical output still ramps.
    const uint8_t range = normal - config.minBrightness;
    const uint8_t firstDrop = range < StimulusPolicy::OutputStepPercent ? range : StimulusPolicy::OutputStepPercent;
    const float strength = ClampValue((limited - EnterScore) / (STIMULUS_MAX_SCORE - EnterScore), 0.0f, 1.0f);
    const uint8_t desired = normal - static_cast<uint8_t>(roundf(firstDrop + (range - firstDrop) * strength));
    mBrightness = ClampValue<uint8_t>(mBrightness, config.minBrightness, normal);
    mCct = ClampValue<uint16_t>(mCct, config.minCCTMireds, config.maxCCTMireds);
    if (!trusted && mState == State::Normal) {
        mHighPending = mLowPending = false;
        if (snapshot.observedOutputs.valid) {
            const auto& observed = snapshot.observedOutputs;
            mBrightness = observed.lightOn ? ClampValue<uint8_t>(observed.brightness, 0, normal) : 0;
            mCct = ClampValue<uint16_t>(observed.cct, config.minCCTMireds, config.maxCCTMireds);
        }
        return {mBrightness > 0, mBrightness, mCct};
    }
    if (mState == State::Normal) {
        mBrightness = normal; mCct = normalCct;
        if (valid && limited >= EnterScore) {
            if (!mHighPending) { mHighPending = true; mHighSince = now; mPendingBrightness = desired; }
            // Confirm only the reduction supported throughout this interval;
            // a single stronger sample at its end must not choose the floor.
            if (desired > mPendingBrightness) mPendingBrightness = desired;
            if (now - mHighSince >= EnterMs) {
                mState = State::Relief;
                mBrightness = ClampValue(mPendingBrightness, config.minBrightness, normal); mCct = reliefCct;
                mReliefSince = now; mHighPending = false;
            }
        } else mHighPending = false;
    } else if (mState == State::Relief) {
        // Apply a saved relief tone independently, even at the brightness floor.
        // System ramps brightness and CCT together; this only changes targets.
        const bool toneChanged = mCct != reliefCct;
        if (toneChanged) {
            mCct = reliefCct; mReliefSince = now;
            mHighPending = mLowPending = false;
        }
        if (!toneChanged && valid && limited >= EnterScore && desired < mBrightness) {
            if (!mHighPending) { mHighPending = true; mHighSince = now; mPendingBrightness = desired; }
            if (desired > mPendingBrightness) mPendingBrightness = desired;
            if (now - mHighSince >= EnterMs) {
                mBrightness = ClampValue(mPendingBrightness, config.minBrightness, normal);
                mReliefSince = now; mHighPending = mLowPending = false;
            }
        } else mHighPending = false;
        const bool canRetry = !toneChanged && valid && mRetry.MayRecover(snapshot, true);
        if (canRetry && limited <= RecoverScore && now - mReliefSince >= config.reliefHoldMs) {
            if (!mLowPending) { mLowPending = true; mLowSince = now; }
            if (now - mLowSince >= config.recoverConfirmMs) {
                mState = State::Recover; mLowPending = false; mRecoverPaused = false; mStepAt = now;
                mRecoveryStart = mLastSafe = mBrightness; mRecoveryStartCct = mCct;
            }
        } else mLowPending = false;
    } else {
        if (!valid) {
            mHighPending = mLowPending = false; mRecoverPaused = mRecoverPaused || !trusted; mStepAt = now;
        } else if (limited >= EnterScore) {
            mLowPending = false; mRecoverPaused = true; mStepAt = now;
            if (!mHighPending) { mHighPending = true; mHighSince = now; }
            if (now - mHighSince >= EnterMs) {
                mState = State::Relief; mReliefSince = now; mHighPending = false;
                mBrightness = mLastSafe; // Undo only the failed step; do not repeat a large drop.
                mCct = reliefCct; mRetry.Block();
            }
        } else if (limited > RecoverScore) {
            // Middle band holds both brightness and tone, without re-entering relief.
            mHighPending = mLowPending = false; mRecoverPaused = true; mStepAt = now;
        } else {
            mHighPending = false;
            if (mRecoverPaused) {
                if (!mLowPending) { mLowPending = true; mLowSince = now; }
                if (now - mLowSince < config.recoverConfirmMs) return {mBrightness > 0, mBrightness, mCct};
                mLowPending = false; mRecoverPaused = false; mStepAt = now;
            }
            if (now - mStepAt >= RecoveryStepMs) {
                mLastSafe = mBrightness;
                const uint8_t remaining = normal - mBrightness;
                mBrightness += remaining > StimulusPolicy::OutputStepPercent ? StimulusPolicy::OutputStepPercent : remaining;
                mStepAt = now;
                const float fraction = normal > mRecoveryStart ? float(normal - mBrightness) / (normal - mRecoveryStart) : 0;
                mCct = static_cast<uint16_t>(normalCct + (mRecoveryStartCct - normalCct) * fraction);
                if (mBrightness == normal) {
                    // Do not resume learning until the last output step is observed stable and low.
                    if (!snapshot.observedOutputs.valid ||
                        (snapshot.observedOutputs.brightness == normal && snapshot.observedOutputs.cct == normalCct)) {
                        mState = State::Normal; mRetry.Reset();
                    }
                }
            }
        }
    }
    return {mBrightness > 0, mBrightness, mCct};
}
