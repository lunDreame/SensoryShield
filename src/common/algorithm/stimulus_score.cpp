#include "common/algorithm/stimulus_score.h"

#include <math.h>

namespace {
float PositiveResidual(const RollingBaseline& baseline, float value, float noiseFloor) {
    if (!baseline.Ready()) {
        return 0.0f;
    }
    return fmaxf(0.0f, value - baseline.Median()) / baseline.Mad(noiseFloor);
}

float ValidWeight(float weight) {
    return isfinite(weight) ? ClampValue(weight, 0.0f, 1.0f) : 0.0f;
}
} // namespace

void StimulusScorer::Reset() {
    mReference = {};
    mLuxBaseline.Reset();
    mSoundBaseline.Reset();
}

StimulusScore StimulusScorer::Evaluate(const SensorSnapshot& snapshot, const AppConfig& config) {
    float lightResidual = 0.0f;
    const bool validLux = snapshot.illuminanceValid && isfinite(snapshot.lux) && snapshot.lux >= 0.0f;
    if (validLux) {
        lightResidual = IsValidEnvironmentBaseline(mReference)
            ? fmaxf(0.0f, snapshot.lux - mReference.luxMedian) / fmaxf(mReference.luxMad, LuxNoiseFloor)
            : PositiveResidual(mLuxBaseline, snapshot.lux, LuxNoiseFloor);
        // Learn normal or darker conditions, while keeping sustained bright input visible as a stimulus.
        if (!IsValidEnvironmentBaseline(mReference) && (!mLuxBaseline.Ready() || lightResidual <= BaselineLearningLimit)) {
            mLuxBaseline.Add(snapshot.lux);
        }
    }

    float soundResidual = 0.0f;
    const bool validSound = snapshot.micValid && snapshot.sound.valid && isfinite(snapshot.sound.energy) &&
                            snapshot.sound.energy >= 0.0f && snapshot.sound.energy <= 1.0f;
    if (validSound) {
        soundResidual = IsValidEnvironmentBaseline(mReference)
            ? fmaxf(0.0f, snapshot.sound.energy - mReference.soundMedian) / fmaxf(mReference.soundMad, SoundNoiseFloor)
            : PositiveResidual(mSoundBaseline, snapshot.sound.energy, SoundNoiseFloor);
        // Sustained noise must not disappear into the learned quiet baseline.
        if (!IsValidEnvironmentBaseline(mReference) && (!mSoundBaseline.Ready() || soundResidual <= BaselineLearningLimit)) {
            mSoundBaseline.Add(snapshot.sound.energy);
        }
    }

    const float lightScore = lightResidual * ValidWeight(config.lightWeight);
    const float soundScore = soundResidual * ValidWeight(config.soundWeight);
    const float combined = ClampValue(lightScore + soundScore, 0.0f, MaximumScore);
    return {lightScore, soundScore, combined};
}
