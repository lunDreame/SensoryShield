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
        mStimulusScorer.SeedEnvironment(initial); mSoundFan.SeedEnvironment(initial);
    }
    // Observe the environment in every mode; automatic output evaluation is separate.
    StimulusScore EvaluateEnvironment(const SensorSnapshot& snapshot);
    ControlTarget Evaluate(const SensorSnapshot& snapshot, const StimulusScore& stimulus);
    void ResetAutomaticLight() { mLightPresence.Reset(); }

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;

  private:
    Algorithm() = default;
    ~Algorithm() = default;

    AppConfig mConfig = {};
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
