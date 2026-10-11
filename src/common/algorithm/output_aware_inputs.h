#pragma once

#include "definition.h"
#include "common/algorithm/light_sensor_calibration.h"
#include <math.h>

// Compare readings taken at unchanged output. The jump across a command/ramp is
// not attributed to the room. This is NOT a calibrated separation of light/sound.
// External changes coincident with a transition remain unobservable without
// hardware calibration. Keep that limitation explicit in commissioning tests.
class OutputAwareSignal {
  public:
    static constexpr uint32_t SettleMs = 500; // Engineering setting, not a clinical duration.
    struct Result { float value; bool valid, learn; };
    void Reset() { *this = {}; }
    Result Observe(float raw, uint64_t output, uint32_t now, bool settled, bool valid) {
        if (!valid || !isfinite(raw)) {
            mReanchor = true; mChangedAt = now;
            return {mValue, false, false};
        }
        if (!mSeen) {
            if (!settled) return {0, false, false};
            mSeen = true; mReference = mOutput = output;
            mLastRaw = mValue = raw; mLastTime = now;
            return {raw, true, true};
        }
        if (now == mLastTime) return {mValue, true, false};
        if (output != mOutput || !settled || now - mLastTime > 1000U) {
            mReanchor = true; mChangedAt = now; mOutput = output;
        }
        mLastTime = now;
        if (mReanchor) {
            if (!settled || now - mChangedAt < SettleMs) return {mValue, true, false};
            // Returning to the original output permits an absolute comparison.
            if (output == mReference) mValue = raw;
            mLastRaw = raw; mReanchor = false;
            return {mValue, true, false};
        }
        mValue = output == mReference ? raw : fmaxf(0, mValue + raw - mLastRaw);
        mLastRaw = raw;
        return {mValue, true, true};
    }
  private:
    bool mSeen = false, mReanchor = false;
    uint64_t mOutput = 0, mReference = 0;
    uint32_t mChangedAt = 0, mLastTime = 0;
    float mValue = 0, mLastRaw = 0;
};

class OutputAwareInputs {
  public:
    void Reset() { mLight.Reset(); mSound.Reset(); }
    void SetLightCalibration(const LightSensorCalibration& calibration) { mCalibration = calibration; Reset(); }
    SensorSnapshot Observe(const SensorSnapshot& input) {
        if (!input.observedOutputs.valid) return input; // Pure algorithm fixtures / legacy callers.
        SensorSnapshot result = input;
        const auto& o = input.observedOutputs;
        uint64_t lightKey = uint64_t(o.lightOn) | (uint64_t(o.brightness) << 1) |
            (uint64_t(o.cct) << 8) | (uint64_t(o.rgb) << 24) |
            (o.rgb ? (uint64_t(o.red) << 25) | (uint64_t(o.green) << 33) | (uint64_t(o.blue) << 41) : 0);
        const uint64_t fanKey = uint64_t(o.fanOn) | (uint64_t(o.fanPercent) << 1);
        float comparableLux = input.lux;
        const bool calibrated = mCalibration.Correct(input.lux, o, comparableLux);
        if (calibrated) lightKey = uint64_t(1) << 63;
        const auto light = mLight.Observe(comparableLux, lightKey, input.observedAtMs,
            o.lightObservationConsistent && (calibrated || o.lightSettled),
            input.illuminanceValid && input.lux >= 0);
        const auto sound = mSound.Observe(input.sound.energy, fanKey, input.sound.timestampMs, o.fanSettled,
            input.micValid && input.sound.valid && isfinite(input.sound.energy) &&
            input.sound.energy >= 0 && input.sound.energy <= 1 && isfinite(input.sound.peak) &&
            isfinite(input.sound.delta) && input.sound.peak >= input.sound.energy && input.sound.peak <= 1 &&
            input.sound.delta >= 0 && input.sound.delta <= 1 &&
            input.observedAtMs - input.sound.timestampMs <= 1000U);
        result.lux = light.value;
        result.illuminanceValid = light.valid;
        result.sound.energy = ClampValue(sound.value, 0.0f, 1.0f);
        result.sound.peak = ClampValue(result.sound.energy + fmaxf(0, input.sound.peak - input.sound.energy), result.sound.energy, 1.0f);
        if (!sound.learn) result.sound.delta = 0;
        result.micValid = result.sound.valid = sound.valid;
        result.learnLight = light.learn; result.learnSound = sound.learn;
        result.controlInputsSettled = light.learn && sound.learn;
        return result;
    }
  private:
    OutputAwareSignal mLight, mSound;
    LightSensorCalibration mCalibration = InstallationLightCalibration;
};
