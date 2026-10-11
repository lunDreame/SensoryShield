#include "common/algorithm/stimulus_score.h"
#include "common/algorithm/stimulus_policy.h"

#include <math.h>

namespace {
float PositiveResidual(const RollingBaseline& baseline, float value, float noiseFloor) {
    if (!baseline.HasSamples()) {
        return 0.0f;
    }
    return fmaxf(0.0f, value - baseline.Median()) / baseline.Mad(noiseFloor);
}

float ValidWeight(float weight) {
    return isfinite(weight) ? ClampValue(weight, 0.0f, 1.0f) : 0.0f;
}
} // namespace

void StimulusScorer::Reset() {
    mLuxBaseline.Reset();
    mSoundBaseline.Reset();
}

bool StimulusScorer::RebaseLight(float lux) {
    if (!isfinite(lux) || lux < 0.0f) return false;
    mLuxBaseline.Reset();
    mLuxBaseline.Add(lux);
    return true;
}

StimulusScore StimulusScorer::Evaluate(const SensorSnapshot& snapshot, const AppConfig& config) {
    const bool wasReady = mLuxBaseline.Ready() && mSoundBaseline.Ready();
    const bool soundWasReady = mSoundBaseline.Ready();
    float lightResidual = 0.0f;
    const bool validLux = snapshot.illuminanceValid && isfinite(snapshot.lux) && snapshot.lux >= 0.0f;
    if (validLux) {
        lightResidual = PositiveResidual(mLuxBaseline, snapshot.lux, LuxNoiseFloor);
    }

    float soundResidual = 0.0f;
    const bool validSound = snapshot.micValid && snapshot.sound.valid && isfinite(snapshot.sound.energy) &&
                            snapshot.sound.energy >= 0.0f && snapshot.sound.energy <= 1.0f;
    if (validSound) {
        soundResidual = PositiveResidual(mSoundBaseline, snapshot.sound.energy, SoundNoiseFloor);
    }

    const float lightScore = lightResidual * ValidWeight(config.lightWeight);
    const float soundScore = soundResidual * ValidWeight(config.soundWeight);
    const float combined = ClampValue(lightScore + soundScore, 0.0f, MaximumScore);
    // Admission uses weighted units with a narrower boundary than relief. Absolute
    // change keeps a large fall from redefining the baseline; displayed/control
    // scores still count increases only. Admission happens before adding samples.
    const float lightDistance = validLux && mLuxBaseline.HasSamples()
        ? fabsf(snapshot.lux - mLuxBaseline.Median()) / mLuxBaseline.Mad(LuxNoiseFloor) * ValidWeight(config.lightWeight) : 0;
    const float soundDistance = validSound && mSoundBaseline.HasSamples()
        ? fabsf(snapshot.sound.energy - mSoundBaseline.Median()) / mSoundBaseline.Mad(SoundNoiseFloor) * ValidWeight(config.soundWeight) : 0;
    const bool ordinary = lightDistance + soundDistance <= StimulusPolicy::LearnScore;
    if (validLux && snapshot.learnLight && ordinary) mLuxBaseline.Add(snapshot.lux);
    if (validSound && snapshot.learnSound && ordinary) mSoundBaseline.Add(snapshot.sound.energy);
    // Report readiness for the baselines used to calculate this score, before this sample was learned.
    return {lightScore, soundScore, combined,
            validLux && validSound && mLuxBaseline.Ready() && mSoundBaseline.Ready() &&
            wasReady, validSound && soundWasReady};
}
