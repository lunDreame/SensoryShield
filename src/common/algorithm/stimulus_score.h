#pragma once

#include "common/algorithm/baseline.h"
#include "definition.h"

struct StimulusScore {
    float light;
    float sound;
    float combined;
};

// Converts valid sensor input into board-independent, one-sided stimulus scores.
class StimulusScorer final {
  public:
    void Reset();
    StimulusScore Evaluate(const SensorSnapshot& snapshot, const AppConfig& config);

  private:
    static constexpr float LuxNoiseFloor = 5.0f;
    static constexpr float SoundNoiseFloor = 0.005f;
    static constexpr float BaselineLearningLimit = 1.5f;
    static constexpr float MaximumScore = 8.0f;

    RollingBaseline mLuxBaseline;
    RollingBaseline mSoundBaseline;
};
