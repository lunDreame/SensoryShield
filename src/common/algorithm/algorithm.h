#pragma once

#include "common/algorithm/light_presence.h"
#include "common/algorithm/sound_fan.h"
#include "common/algorithm/stimulus_score.h"
#include "definition.h"

class Algorithm final {
  public:
    static Algorithm& Instance();

    int Initialize(const AppConfig& config);
    void SetConfig(const AppConfig& config);
    void SetEnvironmentBaseline(const EnvironmentBaseline& baseline);
    // Observe the environment in every mode; automatic output evaluation is separate.
    StimulusScore EvaluateEnvironment(const SensorSnapshot& snapshot);
    ControlTarget Evaluate(const SensorSnapshot& snapshot, const StimulusScore& stimulus);

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;

  private:
    Algorithm() = default;
    ~Algorithm() = default;

    AppConfig mConfig = {};
    StimulusScorer mStimulusScorer;
    LightPresenceController mLightPresence;
    SoundFanController mSoundFan;
    ControlTarget mPreviousTarget = {};
};

inline Algorithm* GetAlgorithm() {
    return &Algorithm::Instance();
}
