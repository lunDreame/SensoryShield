#pragma once

#include "common/algorithm/light_presence.h"
#include "common/algorithm/sound_fan.h"
#include "common/algorithm/stimulus_score.h"
#include "common/algorithm/output_aware_inputs.h"
#include "definition.h"

class Algorithm final {
  public:
    static Algorithm& Instance();

    int Initialize(const AppConfig& config);
    void SetConfig(const AppConfig& config);
    void SeedEnvironment(const InitialEnvironment& initial) {
        mStimulusScorer.SeedEnvironment(initial);
    }
    // Score every mode, but admit ordinary samples only in settled automatic normal output.
    StimulusScore EvaluateEnvironment(const SensorSnapshot& snapshot, bool automaticMode = true);
    ControlTarget Evaluate(const SensorSnapshot& snapshot, const StimulusScore& stimulus);
    void ResetAutomaticLight() { mLightPresence.Reset(); mSoundFan.Reset(); }
    float LightBaselineMedian() const { return mStimulusScorer.LightBaselineMedian(); }
    float LightBaselineMad() const { return mStimulusScorer.LightBaselineMad(); }
    float SoundBaselineMedian() const { return mStimulusScorer.SoundBaselineMedian(); }
    bool LightRebasePending() const { return mLightRebasePending; }
    bool ControlInputsSettled() const { return mObserved.controlInputsSettled; }

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;

  private:
    Algorithm() = default;
    ~Algorithm() = default;

    AppConfig mConfig = {};
    bool mLightRebasePending = false;
    OutputAwareInputs mOutputAware;
    SensorSnapshot mObserved = {};
    StimulusScorer mStimulusScorer;
    LightPresenceController mLightPresence;
    SoundFanController mSoundFan;
    ControlTarget mPreviousTarget = {};
};

inline Algorithm* GetAlgorithm() {
    return &Algorithm::Instance();
}
