#include "common/algorithm/light_presence.h"
#include <math.h>

LightTarget LightPresenceController::Evaluate(const SensorSnapshot& snapshot, const AppConfig& config,
                                              float score, uint32_t now) {
    const uint8_t normal = ClampValue<uint8_t>(config.preferredBrightness, config.minBrightness, config.maxBrightness);
    const uint16_t normalCct = config.minCCTMireds;
    const uint16_t reliefCct = ClampValue<uint16_t>(config.preferredCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    if (!snapshot.occupied) {
        Reset();
        return {false, 0, normalCct}; // Preserve the existing PIR/hold policy in Algorithm.
    }
    const bool trusted = snapshot.illuminanceValid && snapshot.micValid && snapshot.sound.valid && isfinite(score) &&
        (!snapshot.observedOutputs.valid || now - snapshot.observedAtMs <= 1000U);
    const bool valid = trusted && snapshot.controlInputsSettled;
    const float limited = isfinite(score) ? ClampValue(score, 0.0f, STIMULUS_MAX_SCORE) : STIMULUS_MAX_SCORE;
    const uint8_t desired = ClampValue<uint8_t>(static_cast<uint8_t>(normal -
        (normal - config.minBrightness) * limited / STIMULUS_MAX_SCORE), config.minBrightness, normal);
    mBrightness = ClampValue<uint8_t>(mBrightness, config.minBrightness, normal);
    if (!trusted && mState == State::Normal) {
        mHighPending = mLowPending = false;
        // In particular, AUTO after a dim manual state must not brighten on sensor failure.
        if (snapshot.observedOutputs.valid) {
            const auto& observed = snapshot.observedOutputs;
            mBrightness = observed.lightOn ? ClampValue<uint8_t>(observed.brightness, 0, normal) : 0;
            return {mBrightness > 0, mBrightness,
                ClampValue<uint16_t>(observed.cct, config.minCCTMireds, config.maxCCTMireds)};
        }
        return {mBrightness > 0, mBrightness, normalCct};
    }
    if (mState == State::Normal) {
        mBrightness = normal;
        if (valid && limited >= EnterScore) {
            if (!mHighPending) { mHighPending = true; mHighSince = now; }
            if (now - mHighSince >= EnterMs) {
                mState = State::Relief; mBrightness = desired; mReliefSince = now; mHighPending = false;
            }
        } else mHighPending = false;
    } else if (mState == State::Relief) {
        if (valid && limited >= EnterScore && desired < mBrightness) mBrightness = desired;
        if (valid && limited <= RecoverScore && now - mReliefSince >= MinimumHoldMs) {
            if (!mLowPending) { mLowPending = true; mLowSince = now; }
            if (now - mLowSince >= RecoverMs) {
                mState = State::Recover; mLowPending = false; mStepAt = now;
            }
        } else mLowPending = false;
    } else {
        if (valid && limited > RecoverScore) {
            mState = State::Relief; mReliefSince = now; mLowPending = false;
            if (limited >= EnterScore && desired < mBrightness) mBrightness = desired;
        } else if (valid && now - mStepAt >= RecoveryStepMs) {
            const uint8_t remaining = normal - mBrightness;
            mBrightness = static_cast<uint8_t>(mBrightness + (remaining > 2U ? 2U : remaining));
            mStepAt = now;
            if (mBrightness == normal) mState = State::Normal;
        }
    }
    uint16_t cct = normalCct;
    if (mState == State::Relief) cct = reliefCct;
    else if (mState == State::Recover && normal > config.minBrightness) {
        const float amount = float(normal - mBrightness) / float(normal - config.minBrightness);
        cct = static_cast<uint16_t>(normalCct + (reliefCct - normalCct) * amount);
    }
    return {mBrightness > 0, mBrightness, cct};
}
