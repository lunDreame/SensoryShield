#pragma once

#include "common/algorithm/baseline.h"
#include "definition.h"
#include <math.h>

// Board-independent policy. Percentages/times are provisional until measured on the fan.
class SoundFanController final {
  public:
    static constexpr uint32_t StaleMs = 1000;
    static constexpr uint32_t EnterQuietMs = 1500;
    static constexpr uint32_t RecoverMs = 3000;
    static constexpr uint8_t NormalPercent = 30;
    static constexpr uint8_t QuietPercent = FAN_MIN_DUTY_PERCENT;

    void Reset() {
        mBaseline.Reset();
        mSeen = mQuiet = mHighPending = mLowPending = false;
        mPercent = 0;
        mLastSample = mLastUpdate = mPendingSince = 0;
    }

    uint8_t Evaluate(const SensorSnapshot& snapshot, const AppConfig& config, uint32_t now) {
        const uint8_t cap = config.fanMaxPercent > 100 ? 100 : config.fanMaxPercent;
        // A lower nonzero target would be raised by the PWM driver to its physical minimum.
        if (!snapshot.pirValid || !snapshot.occupied || cap < FAN_MIN_DUTY_PERCENT) {
            mPercent = 0;
            mHighPending = mLowPending = false;
            mLastUpdate = now;
            return 0;
        }
        if (mPercent > cap) mPercent = cap;
        const auto& sound = snapshot.sound;
        const bool valid = snapshot.micValid && sound.valid &&
            isfinite(sound.energy) && isfinite(sound.peak) && isfinite(sound.delta) &&
            sound.energy >= 0 && sound.energy <= 1 && sound.peak >= sound.energy && sound.peak <= 1 &&
            sound.delta >= 0 && sound.delta <= 1 && (now - sound.timestampMs) <= StaleMs;
        if (!valid) {
            mHighPending = mLowPending = false;
            // Never increase output when input cannot be trusted; do not interpret failure as silence.
            if (mPercent > QuietPercent) mPercent = QuietPercent;
            mLastUpdate = now;
            return mPercent;
        }
        if (mSeen && sound.timestampMs == mLastSample) return mPercent;
        if (mSeen && (sound.timestampMs - mLastSample) > StaleMs) {
            mHighPending = mLowPending = false;
        }
        mLastSample = sound.timestampMs;
        mSeen = true;

        const bool ready = mBaseline.Ready();
        const float median = mBaseline.Median();
        const float scale = mBaseline.Mad(0.005f);
        const float upward = ready ? fmaxf(0.0f, sound.energy - median) / scale : 0.0f;
        const float weight = isfinite(config.soundWeight) ? ClampValue(config.soundWeight, 0.0f, 1.0f) : 0.0f;
        const float severity = upward * weight;
        const bool high = ready && severity >= 3.0f;
        const bool low = ready && severity <= 1.5f;
        const bool impulse = ready && sound.delta > 3.0f * scale && sound.peak > median + 6.0f * scale;
        // Compare before learning. Elevated intervals must not become the new quiet baseline.
        if (!ready || (upward <= 1.5f && !impulse)) mBaseline.Add(sound.energy);

        if (!mQuiet) {
            mLowPending = false;
            if (high) {
                if (!mHighPending) { mHighPending = true; mPendingSince = sound.timestampMs; }
                if ((sound.timestampMs - mPendingSince) >= EnterQuietMs) {
                    mQuiet = true;
                    mHighPending = false;
                }
            } else mHighPending = false;
        } else {
            mHighPending = false;
            if (low || weight == 0.0f) {
                if (!mLowPending) { mLowPending = true; mPendingSince = sound.timestampMs; }
                if ((sound.timestampMs - mPendingSince) >= RecoverMs) {
                    mQuiet = false;
                    mLowPending = false;
                }
            } else mLowPending = false;
        }

        // Warm-up and confirmed noise use quiet output. Short isolated peaks do not change the mode.
        const uint8_t desired = ClampValue<uint8_t>(!ready || mQuiet ? QuietPercent : NormalPercent, 0, cap);
        if (mPercent == 0) {
            mPercent = FAN_MIN_DUTY_PERCENT;
        } else if ((now - mLastUpdate) >= ALGORITHM_PERIOD_MS) {
            const uint8_t step = 2;
            if (mPercent < desired) mPercent += desired - mPercent > step ? step : desired - mPercent;
            if (mPercent > desired) mPercent -= mPercent - desired > step ? step : mPercent - desired;
        }
        mLastUpdate = now;
        return mPercent;
    }

  private:
    RollingBaseline mBaseline;
    bool mSeen = false, mQuiet = false, mHighPending = false, mLowPending = false;
    uint8_t mPercent = 0;
    uint32_t mLastSample = 0, mLastUpdate = 0, mPendingSince = 0;
};
