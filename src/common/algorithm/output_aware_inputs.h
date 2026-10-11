#pragma once
#include "definition.h"
#include <math.h>

// Observe total room inputs. Own-output transitions delay decisions/learning,
// but do not subtract the achieved reduction from the measured environment.
class OutputAwareSignal {
  public:
    static constexpr uint32_t SettleMs = 500; // Engineering setting, verify on the installed board.
    struct Result { float value; bool valid, learn; };
    void Reset() { *this = {}; }
    Result Observe(float raw, uint64_t output, uint32_t now, bool settled, bool valid) {
        if (!valid || !isfinite(raw)) {
            mWaiting = true; mChangedAt = now;
            return {mLastRaw, false, false};
        }
        if (!mSeen) {
            mSeen = true; mOutput = output; mLastTime = now; mLastRaw = raw;
            mWaiting = !settled; mChangedAt = now;
            return {raw, true, settled};
        }
        if (now == mLastTime) return {raw, true, false};
        if (output != mOutput || !settled || now - mLastTime > 1000U) {
            mWaiting = true; mChangedAt = now; mOutput = output;
        }
        mLastTime = now; mLastRaw = raw;
        if (mWaiting) {
            if (!settled || now - mChangedAt < SettleMs) return {raw, true, false};
            mWaiting = false;
        }
        return {raw, true, true};
    }
  private:
    bool mSeen = false, mWaiting = false;
    uint64_t mOutput = 0;
    uint32_t mChangedAt = 0, mLastTime = 0;
    float mLastRaw = 0;
};

inline uint64_t LightOutputKey(const ObservedOutputs& o) {
    return uint64_t(o.lightOn) | (uint64_t(o.brightness) << 1) | (uint64_t(o.cct) << 8) |
        (uint64_t(o.rgb) << 24) | (o.rgb ? (uint64_t(o.red) << 25) |
        (uint64_t(o.green) << 33) | (uint64_t(o.blue) << 41) : 0);
}
inline uint64_t FanOutputKey(const ObservedOutputs& o) {
    return uint64_t(o.fanOn) | (uint64_t(o.fanPercent) << 1);
}
class OutputAwareInputs {
  public:
    void Reset() { mLight.Reset(); mSound.Reset(); }
    SensorSnapshot Observe(const SensorSnapshot& input) {
        if (!input.observedOutputs.valid) return input; // Pure algorithm fixtures / legacy callers.
        SensorSnapshot result = input;
        const auto& o = input.observedOutputs;
        const auto light = mLight.Observe(input.lux, LightOutputKey(o), input.observedAtMs,
            o.lightObservationConsistent && o.lightSettled, input.illuminanceValid && input.lux >= 0);
        const auto sound = mSound.Observe(input.sound.energy, FanOutputKey(o), input.sound.timestampMs,
            o.fanOutputValid && !o.fanBoosting && o.fanSettled,
            input.micValid && input.sound.valid && isfinite(input.sound.energy) &&
            input.sound.energy >= 0 && input.sound.energy <= 1 && isfinite(input.sound.peak) &&
            isfinite(input.sound.delta) && input.sound.peak >= input.sound.energy && input.sound.peak <= 1 &&
            input.sound.delta >= 0 && input.sound.delta <= 1 &&
            input.observedAtMs - input.sound.timestampMs <= 1000U);
        result.lux = light.value;
        result.illuminanceValid = light.valid;
        result.micValid = result.sound.valid = sound.valid;
        result.learnLight = input.learnLight && light.learn;
        result.learnSound = input.learnSound && sound.learn;
        result.controlInputsSettled = input.controlInputsSettled && light.learn && sound.learn;
        return result;
    }
  private:
    OutputAwareSignal mLight, mSound;
};
