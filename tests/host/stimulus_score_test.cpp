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
    for (int i = 0; i < ENVIRONMENT_SAMPLE_CAPACITY; ++i) {
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
        assert(fabsf(brighter.light - 13.75f) < 0.001f);
        assert(brighter.sound == 0.0f);
        assert(brighter.combined == STIMULUS_MAX_SCORE);

        const StimulusScore darker = scorer.Evaluate(Snapshot(20.0f), config);
        assert(darker.light == 0.0f);
        assert(darker.combined == 0.0f);
    }

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        const StimulusScore louder = scorer.Evaluate(Snapshot(100.0f, 0.06f), config);
        assert(louder.light == 0.0f);
        assert(fabsf(louder.sound - 9.0f) < 0.001f);
        assert(louder.combined == STIMULUS_MAX_SCORE);

        for (int i = 0; i < 40; ++i) {
            const StimulusScore sustained = scorer.Evaluate(Snapshot(100.0f, 0.06f), config);
            assert(fabsf(sustained.sound - 9.0f) < 0.001f);
        }
    }

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        const StimulusScore maximum = scorer.Evaluate(Snapshot(200.0f, 0.10f), config);
        assert(maximum.combined == STIMULUS_MAX_SCORE);
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

    {
        StimulusScorer scorer;
        Warm(scorer, config);
        // Each weighted channel is below entry on its own, but together
        // they trigger relief: that sustained interval must not become ordinary.
        for (int i = 0; i < 120; ++i) {
            const auto mixed = scorer.Evaluate(Snapshot(103, .0132f), config);
            assert(mixed.combined >= 1.4f);
            assert(fabsf(mixed.sound - .576f) < .001f);
        }
        assert(scorer.LightBaselineMedian() == 100);
        assert(scorer.Evaluate(Snapshot(), config).combined == 0);
    }
    {
        StimulusScorer scorer;
        Warm(scorer, config);
        // An external large decrease must not redefine ordinary conditions,
        // then cause false relief when the original environment returns.
        for (int i = 0; i < 120; ++i) {
            assert(scorer.Evaluate(Snapshot(40, .001f), config).combined == 0);
        }
        assert(scorer.LightBaselineMedian() == 100);
        for (int i = 0; i < 240; ++i) assert(scorer.Evaluate(Snapshot(), config).combined == 0);
        // Small ordinary shifts still update the rolling comparison.
        for (int i = 0; i < 31; ++i) scorer.Evaluate(Snapshot(101, .011f), config);
        assert(scorer.LightBaselineMedian() == 101);
        assert(scorer.Evaluate(Snapshot(101, .011f), config).sound == 0);
        // Command-affected observations never enter learning, even near baseline.
        auto controlled = Snapshot(102, .012f);
        controlled.learnLight = controlled.learnSound = false;
        for (int i = 0; i < 40; ++i) scorer.Evaluate(controlled, config);
        assert(scorer.LightBaselineMedian() == 101);
        assert(scorer.Evaluate(Snapshot(101, .011f), config).sound == 0);
    }
    {
        StimulusScorer small; Warm(small, config);
        for (int i=0;i<31;++i) small.Evaluate(Snapshot(102), config);
        assert(small.LightBaselineMedian() == 102);
        assert(small.Evaluate(Snapshot(102), config).combined == 0);
        // A smaller gain admits a larger ordinary change in the same score units.
        config.lightWeight = .3f; StimulusScorer low; Warm(low, config);
        for (int i=0;i<31;++i) low.Evaluate(Snapshot(104), config);
        assert(low.LightBaselineMedian() == 104);
    }
    {
        // Keep the observation-only band out of learning, including gradual staircase inputs.
        StimulusScorer edge; AppConfig c; c.lightWeight = .35f; Warm(edge, c);
        for(int i=0;i<31;++i) edge.Evaluate(Snapshot(104),c); // Exactly 0.7 is admitted.
        assert(edge.LightBaselineMedian()==104);
        StimulusScorer decrease; Warm(decrease, c);
        for(int i=0;i<31;++i) decrease.Evaluate(Snapshot(96),c);
        assert(decrease.LightBaselineMedian()==96); // Same inclusive boundary for decreases.
        AppConfig defaults;
        StimulusScorer above; Warm(above, defaults);
        for(int i=0;i<60;++i) above.Evaluate(Snapshot(103),defaults);
        assert(above.LightBaselineMedian()==100); // Score 0.825 is observation-only.
        StimulusScorer combined; Warm(combined, defaults);
        for(int i=0;i<60;++i) combined.Evaluate(Snapshot(102,.011f),defaults);
        assert(combined.LightBaselineMedian()==100); // 0.55 light + 0.18 sound exceeds 0.7.
        AppConfig normal; StimulusScorer gap; Warm(gap, normal);
        for(int phase=1;phase<=4;++phase) {
            for(int i=0;i<16;++i) gap.Evaluate(Snapshot(100+4.7f*phase),normal);
            assert(gap.LightBaselineMedian()==100);
        }
    }
    puts("adaptive stimulus scoring and ordinary-only learning passed");
}
