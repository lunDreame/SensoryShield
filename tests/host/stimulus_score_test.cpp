#include "common/algorithm/stimulus_score.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
SensorSnapshot Snapshot(float lux = 100.0f, float energy = 0.01f) {
    return {lux, {energy, energy, 0.0f, 0, true}, true, 0, true, true, true};
}

void Warm(StimulusScorer& scorer, const AppConfig& config) {
    for (int i = 0; i < 5; ++i) {
        scorer.Evaluate(Snapshot(), config);
    }
}
} // namespace

int main() {
    AppConfig config;

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        const StimulusScore brighter = scorer.Evaluate(Snapshot(150.0f), config);
        assert(fabsf(brighter.light - 5.5f) < 0.001f);
        assert(brighter.sound == 0.0f);
        assert(fabsf(brighter.combined - 5.5f) < 0.001f);

        const StimulusScore darker = scorer.Evaluate(Snapshot(20.0f), config);
        assert(darker.light == 0.0f);
        assert(darker.combined == 0.0f);
    }

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        const StimulusScore louder = scorer.Evaluate(Snapshot(100.0f, 0.06f), config);
        assert(louder.light == 0.0f);
        assert(fabsf(louder.sound - 4.5f) < 0.001f);
        assert(fabsf(louder.combined - 4.5f) < 0.001f);

        for (int i = 0; i < 40; ++i) {
            const StimulusScore sustained = scorer.Evaluate(Snapshot(100.0f, 0.06f), config);
            assert(fabsf(sustained.sound - 4.5f) < 0.001f);
        }
    }

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        const StimulusScore maximum = scorer.Evaluate(Snapshot(200.0f, 0.10f), config);
        assert(maximum.combined == 8.0f);
    }

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        SensorSnapshot invalid = Snapshot();
        invalid.lux = std::numeric_limits<float>::quiet_NaN();
        invalid.sound.energy = std::numeric_limits<float>::quiet_NaN();
        const StimulusScore score = scorer.Evaluate(invalid, config);
        assert(score.light == 0.0f);
        assert(score.sound == 0.0f);
        assert(score.combined == 0.0f);
    }

    {
        StimulusScorer scorer;
        AppConfig disabled = config;
        disabled.lightWeight = 0.0f;
        disabled.soundWeight = 0.0f;
        Warm(scorer, disabled);
        assert(scorer.Evaluate(Snapshot(200.0f, 0.10f), disabled).combined == 0.0f);
    }

    puts("adaptive stimulus scoring passed");
}
