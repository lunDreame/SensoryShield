#pragma once
#include "common/algorithm/output_aware_inputs.h"
#include "common/algorithm/stimulus_score.h"

class RecoveryGuard {
  public:
    void Reset() { *this = {}; }
    bool IsBlocked() const { return mBlocked; }
    void Block() { mBlocked = true; mAnchored = false; }
    bool MayRecover(const SensorSnapshot& s, bool includeLight) {
        if (!mBlocked) return true;
        const uint64_t key = s.observedOutputs.valid
            ? LightOutputKey(s.observedOutputs) | (FanOutputKey(s.observedOutputs) << 49) : 0;
        if (!mAnchored || key != mOutput) {
            mAnchored = true; mOutput = key; mLux = s.lux; mSound = s.sound.energy;
            return false;
        }
        // Existing measurement floors, not clinical improvement thresholds.
        if ((includeLight && s.lux <= mLux - StimulusScorer::LuxNoiseFloor) ||
            s.sound.energy <= mSound - StimulusScorer::SoundNoiseFloor) {
            Reset(); return true;
        }
        return false;
    }
  private:
    bool mBlocked = false, mAnchored = false;
    uint64_t mOutput = 0;
    float mLux = 0, mSound = 0;
};
