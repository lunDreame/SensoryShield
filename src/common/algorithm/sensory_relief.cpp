#include "common/algorithm/sensory_relief.h"
#include <math.h>

SensoryTarget SensoryReliefController::Evaluate(const SensorSnapshot& snapshot, const AppConfig& config,
                                              float score, uint32_t now) {
    const uint8_t normal = ClampValue<uint8_t>(config.preferredBrightness, config.minBrightness, config.maxBrightness);
    const uint16_t normalCct = ClampValue<uint16_t>(config.normalCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    const uint16_t reliefCct = ClampValue<uint16_t>(config.preferredCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    const uint8_t normalFan = NormalizeFanPercent(config.preferredFanPercent, config.fanMaxPercent);
    const uint8_t quietFan = ClampValue<uint8_t>(NormalizeFanPercent(config.reliefFanPercent, config.fanMaxPercent), 0, normalFan);
    if (config.occupancyAutomationEnabled && (!snapshot.pirValid || !snapshot.occupied)) {
        Reset(); return {false, 0, normalCct, 0};
    }
    mFan = ClampValue<uint8_t>(mFan, 0, normalFan);
    const auto& sound = snapshot.sound;
    const bool trusted = snapshot.illuminanceValid && isfinite(snapshot.lux) && snapshot.lux >= 0 &&
        snapshot.micValid && sound.valid && isfinite(score) && score >= 0 &&
        isfinite(sound.energy) && isfinite(sound.peak) && isfinite(sound.delta) &&
        sound.energy >= 0 && sound.energy <= 1 && sound.peak >= sound.energy && sound.peak <= 1 &&
        sound.delta >= 0 && sound.delta <= 1 && now - sound.timestampMs <= 1000U &&
        (!snapshot.observedOutputs.valid || now - snapshot.observedAtMs <= 1000U);
    mBrightness = ClampValue<uint8_t>(mBrightness, config.minBrightness, normal);
    mCct = ClampValue<uint16_t>(mCct, config.minCCTMireds, config.maxCCTMireds);
    if (!snapshot.controlInputsSettled) { mHighPending = mLowPending = false; mStepAt = now; }
    if (trusted && mSeen && sound.timestampMs == mLastSample) return Target();
    if (trusted && mSeen && sound.timestampMs - mLastSample > 1000U) {
        mHighPending = mLowPending = false; mRecoverPaused = true;
    }
    if (trusted) { mLastSample = sound.timestampMs; mSeen = true; }
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
        // Invalid input cannot confirm a recovery; keep fan at the configured safe output.
        mFan = quietFan;
        if (snapshot.observedOutputs.valid) {
            const auto& observed = snapshot.observedOutputs;
            mBrightness = observed.lightOn ? ClampValue<uint8_t>(observed.brightness, 0, normal) : 0;
            mCct = ClampValue<uint16_t>(observed.cct, config.minCCTMireds, config.maxCCTMireds);
        }
        return Target();
    }
    if (mState == State::Normal) {
        mBrightness = normal; mCct = normalCct; mFan = normalFan; mFanStepAt = now;
        if (valid && limited >= EnterScore) {
            if (!mHighPending) { mHighPending = true; mHighSince = now; mPendingBrightness = desired; }
            // Confirm only the reduction supported throughout this interval;
            // a single stronger sample at its end must not choose the floor.
            if (desired > mPendingBrightness) mPendingBrightness = desired;
            if (now - mHighSince >= EnterMs) {
                mState = State::Relief;
                mBrightness = ClampValue(mPendingBrightness, config.minBrightness, normal); mCct = reliefCct;
                mReliefSince = now; mHighPending = false; mFanStepAt = now - ALGORITHM_PERIOD_MS;
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
        const bool atRelief = mFan <= quietFan || mRetry.IsBlocked();
        const bool canRetry = !toneChanged && valid && atRelief && mRetry.MayRecover(snapshot, true);
        if (canRetry && limited <= RecoverScore && now - mReliefSince >= config.reliefHoldMs) {
            if (!mLowPending) { mLowPending = true; mLowSince = now; }
            if (now - mLowSince >= config.recoverConfirmMs) {
                mState = State::Recover; mLowPending = false; mRecoverPaused = false; mStepAt = now;
                mRecoveryStart = mLastSafe = mBrightness; mRecoveryFan = mLastSafeFan = mFan; mRecoveryStartCct = mCct;
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
                mBrightness = ClampValue<uint8_t>(mLastSafe, config.minBrightness, normal); mFan = ClampValue<uint8_t>(mLastSafeFan, 0, normalFan); // Undo the failed pair of steps.
                mCct = reliefCct; mRetry.Block();
            }
        } else if (limited > RecoverScore) {
            // Middle band holds both brightness and tone, without re-entering relief.
            mHighPending = mLowPending = false; mRecoverPaused = true; mStepAt = now;
        } else {
            mHighPending = false;
            if (mRecoverPaused) {
                if (!mLowPending) { mLowPending = true; mLowSince = now; }
                if (now - mLowSince < config.recoverConfirmMs) return Target();
                mLowPending = false; mRecoverPaused = false; mStepAt = now;
            }
            if (now - mStepAt >= RecoveryStepMs) {
                mLastSafe = mBrightness; mLastSafeFan = mFan;
                const uint8_t fanRemaining = normalFan - mFan;
                mFan += fanRemaining > StimulusPolicy::OutputStepPercent ? StimulusPolicy::OutputStepPercent : fanRemaining;
                const uint8_t remaining = normal - mBrightness;
                mBrightness += remaining > StimulusPolicy::OutputStepPercent ? StimulusPolicy::OutputStepPercent : remaining;
                mStepAt = now;
                const float lightFraction = normal > mRecoveryStart ? float(normal - mBrightness) / (normal - mRecoveryStart) : 0;
                const float fanFraction = normalFan > mRecoveryFan ? float(normalFan - mFan) / (normalFan - mRecoveryFan) : 0;
                const float fraction = lightFraction > fanFraction ? lightFraction : fanFraction;
                mCct = static_cast<uint16_t>(normalCct + (mRecoveryStartCct - normalCct) * fraction);
                if (mBrightness == normal && mFan == normalFan) {
                    // Do not resume learning until the last output step is observed stable and low.
                    if (!snapshot.observedOutputs.valid ||
                        (snapshot.observedOutputs.brightness == normal && snapshot.observedOutputs.cct == normalCct && snapshot.observedOutputs.fanPercent == normalFan)) {
                        mState = State::Normal; mRetry.Reset();
                    }
                }
            }
        }
    }
    // Continue the downward fan ramp even while actual outputs are settling.
    // A failed recovery holds the safe pair until the environment improves.
    if (mState == State::Relief && !mRetry.IsBlocked() && now - mFanStepAt >= ALGORITHM_PERIOD_MS) {
        if (mFan > quietFan) mFan -= mFan - quietFan > StimulusPolicy::OutputStepPercent ? StimulusPolicy::OutputStepPercent : mFan - quietFan;
        mFanStepAt = now;
    }
    if (!trusted) { mHighPending = mLowPending = false; if (mFan > quietFan) mFan = quietFan; }
    return Target();
}
