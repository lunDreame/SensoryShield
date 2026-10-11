#pragma once

#include "common/algorithm/baseline.h"
#include <math.h>

// Saved observations, not an immutable baseline or a calibrated sound-pressure level.
struct InitialEnvironment {
    uint32_t count = 0;
    float lux[ENVIRONMENT_SAMPLE_CAPACITY] = {};
    float sound[ENVIRONMENT_SAMPLE_CAPACITY] = {};

    bool Valid() const {
        if (count != ENVIRONMENT_SAMPLE_CAPACITY) return false;
        for (uint32_t i = 0; i < count; ++i) {
            if (!isfinite(lux[i]) || lux[i] < 0 || !isfinite(sound[i]) || sound[i] < 0 || sound[i] > 1) return false;
        }
        return true;
    }
    void Seed(RollingBaseline& light, RollingBaseline& audio) const {
        light.Reset(); audio.Reset();
        if (!Valid()) return;
        for (uint32_t i = 0; i < count; ++i) { light.Add(lux[i]); audio.Add(sound[i]); }
    }
};

enum class MeasurementState : uint8_t { Idle, Collecting, Ready, Error };

class EnvironmentMeasurement final {
  public:
    // Engineering timeout for failed/stale input; not a comfort or clinical threshold.
    static constexpr uint32_t TimeoutMs = 15000;
    void Start(uint32_t now, uint8_t brightness, uint16_t cct) {
        mData = {}; mState = MeasurementState::Collecting; mStarted = now;
        mLastSound = 0; mSeen = false; mBrightness = brightness; mCct = cct;
    }
    void Cancel() { mState = MeasurementState::Idle; mData = {}; }
    void Invalidate() { if (mState == MeasurementState::Collecting) { mState = MeasurementState::Error; mData = {}; } }
    void Observe(const SensorSnapshot& input, bool settled, uint32_t now) {
        if (mState != MeasurementState::Collecting) return;
        if (now - mStarted >= TimeoutMs) { Invalidate(); return; }
        if (!settled) { mData = {}; mSeen = false; return; }
        if (!input.illuminanceValid || !input.micValid || !input.sound.valid ||
            !isfinite(input.lux) || input.lux < 0 || !isfinite(input.sound.energy) ||
            input.sound.energy < 0 || input.sound.energy > 1 || !isfinite(input.sound.peak) ||
            input.sound.peak < input.sound.energy || input.sound.peak > 1 || !isfinite(input.sound.delta) ||
            input.sound.delta < 0 || input.sound.delta > 1 || now - input.sound.timestampMs > 1000U ||
            (mSeen && input.sound.timestampMs == mLastSound)) return;
        mSeen = true; mLastSound = input.sound.timestampMs;
        const uint32_t i = mData.count++;
        mData.lux[i] = input.lux; mData.sound[i] = input.sound.energy;
        if (mData.count == ENVIRONMENT_SAMPLE_CAPACITY) mState = MeasurementState::Ready;
    }
    MeasurementState State() const { return mState; }
    const InitialEnvironment& Data() const { return mData; }
    bool Matches(uint8_t brightness, uint16_t cct) const { return brightness == mBrightness && cct == mCct; }
  private:
    InitialEnvironment mData;
    MeasurementState mState = MeasurementState::Idle;
    uint32_t mStarted = 0, mLastSound = 0;
    bool mSeen = false;
    uint8_t mBrightness = 0;
    uint16_t mCct = 0;
};
