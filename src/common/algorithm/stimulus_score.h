#pragma once

#include "common/algorithm/baseline.h"
#include "definition.h"
#include "common/algorithm/environment_measurement.h"

struct StimulusScore {
    float light;
    float sound;
    float combined;
    bool baselineReady = false;
};

// Converts valid sensor input into board-independent, one-sided stimulus scores.
class StimulusScorer final {
  public:
    void Reset();
    void SeedEnvironment(const InitialEnvironment& initial) { initial.Seed(mLuxBaseline, mSoundBaseline); }
    StimulusScore Evaluate(const SensorSnapshot& snapshot, const AppConfig& config);
    // Read-only diagnostics for host simulations; these do not change learning.
    float LightBaselineMedian() const { return mLuxBaseline.Median(); }
    float LightBaselineMad() const { return mLuxBaseline.Mad(LuxNoiseFloor); }

  private:
    static constexpr float LuxNoiseFloor = 2.0f;
    static constexpr float SoundNoiseFloor = 0.0025f;
    static constexpr float BaselineLearningLimit = 1.5f;
    static constexpr float MaximumScore = STIMULUS_MAX_SCORE;

    RollingBaseline mLuxBaseline;
    RollingBaseline mSoundBaseline;
};
