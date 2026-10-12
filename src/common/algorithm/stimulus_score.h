#pragma once

#include "common/algorithm/baseline.h"
#include "definition.h"
#include "common/algorithm/environment_measurement.h"

struct StimulusScore {
    float light;
    float sound;
    float combined;
    bool baselineReady = false;
    bool soundBaselineReady = false;
};

// Positive change / raw MAD, with engineering floors and gains. This is neither
// NIST's modified Z-score (0.6745 scaling) nor an individual's clinical threshold.
// Numeric rationale and learning policy: docs/algorithm-threshold-review.json.
class StimulusScorer final {
  public:
    void Reset();
    bool RebaseLight(float lux);
    bool RebaseSound(float rms);
    void SeedEnvironment(const InitialEnvironment& initial) { initial.Seed(mLuxBaseline, mSoundBaseline); }
    StimulusScore Evaluate(const SensorSnapshot& snapshot, const AppConfig& config);
    // Read-only diagnostics for host simulations; these do not change learning.
    float LightBaselineMedian() const { return mLuxBaseline.Median(); }
    float LightBaselineMad() const { return mLuxBaseline.Mad(LuxNoiseFloor); }
    float SoundBaselineMedian() const { return mSoundBaseline.Median(); }
    static constexpr float LuxNoiseFloor = 2.0f; // lx, not the sensor's absolute accuracy tolerance.
    static constexpr float SoundNoiseFloor = 0.0025f; // Normalized PCM RMS amplitude, not dB SPL.

  private:
    static constexpr float MaximumScore = STIMULUS_MAX_SCORE;

    RollingBaseline mLuxBaseline;
    RollingBaseline mSoundBaseline;
};
