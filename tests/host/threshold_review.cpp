#include "common/algorithm/stimulus_score.h"
#include "common/algorithm/sensory_relief.h"
#include "fan_probe.h"
#include <cassert>
#include <cmath>
#include <cstdio>

// Characterize the real policies, including known limitations. Synthetic values
// are not a sensor calibration, a clinical threshold, or an optimization result.
uint32_t testUptimeMs = 0;
namespace {
SensorSnapshot Sample(float lux = 100, float rms = .01f, uint32_t now = 0) {
    SensorSnapshot s = {lux, {rms, rms, 0, now, true}, true, now, true, true, true};
    s.observedAtMs = now;
    return s;
}
void Warm(StimulusScorer& scorer, const AppConfig& c, float lux = 100, float rms = .01f) {
    for (int i = 0; i < ENVIRONMENT_SAMPLE_CAPACITY; ++i) scorer.Evaluate(Sample(lux, rms), c);
}
struct Entry { float delta; uint8_t targetBrightness; };
Entry LightEntry(float gain, bool soundOnly) {
    // Independent sustained steps on an initially constant baseline. The real
    // learning gate remains enabled; no guessed lamp-to-lux mapping is needed.
    const float resolution = soundOnly ? .00001f : .01f;
    for (int n = 1; n <= 10000; ++n) {
        const float delta = n * resolution;
        AppConfig c;
        if (soundOnly) c.soundWeight = gain; else c.lightWeight = gain;
        StimulusScorer scorer; SensoryReliefController light;
        Warm(scorer, c);
        for (uint32_t now = 0; now <= SensoryReliefController::EnterMs; now += ALGORITHM_PERIOD_MS) {
            auto s = Sample(soundOnly ? 100 : 100 + delta, soundOnly ? .01f + delta : .01f, now);
            auto target = light.Evaluate(s, c, scorer.Evaluate(s, c).combined, now);
            if (target.brightnessPercent < c.preferredBrightness) return {delta, target.brightnessPercent};
        }
    }
    assert(false && "entry not found in the declared sweep range");
    return {0, 0};
}
float FanEntry(float gain) {
    for (int n = 1; n <= 1000; ++n) {
        const float delta = n * .0001f;
        AppConfig c; c.soundWeight = gain;
        FanProbe fan; StimulusScorer scorer;
        for (uint32_t now = 0; now < 25000; now += ALGORITHM_PERIOD_MS) fan.Evaluate(Sample(100, .01f, now), c, scorer.Evaluate(Sample(100, .01f, now), c), now);
        for (uint32_t now = 25000; now <= 25000 + FanProbe::EnterQuietMs; now += ALGORITHM_PERIOD_MS) {
            if (fan.Evaluate(Sample(100, .01f + delta, now), c, scorer.Evaluate(Sample(100, .01f + delta, now), c), now) < c.preferredFanPercent) return delta;
        }
    }
    assert(false && "fan entry not found in the declared sweep range");
    return 0;
}
} // namespace
int main() {
    AppConfig c;
    const float lightGains[] = {.3f, .55f, .8f}, soundGains[] = {.25f, .45f, .75f};
    Entry lightEntries[3], soundEntries[3]; float fanEntries[3];
    for (int i = 0; i < 3; ++i) {
        lightEntries[i] = LightEntry(lightGains[i], false);
        soundEntries[i] = LightEntry(soundGains[i], true);
        fanEntries[i] = FanEntry(soundGains[i]);
    }
    assert(lightEntries[0].delta > lightEntries[1].delta && lightEntries[1].delta > lightEntries[2].delta);
    assert(soundEntries[0].delta > soundEntries[1].delta && soundEntries[1].delta > soundEntries[2].delta);
    assert(fanEntries[0] > fanEntries[1] && fanEntries[1] > fanEntries[2]);
    for (int i = 0; i < 3; ++i) assert(fabsf(fanEntries[i] - soundEntries[i].delta) <= .0001f);
    StimulusScorer fresh; int firstReady = 0;
    for (int i = 1; i <= ENVIRONMENT_SAMPLE_CAPACITY + 1; ++i) {
        if (fresh.Evaluate(Sample(), c).baselineReady && !firstReady) firstReady = i;
    }
    assert(firstReady == ENVIRONMENT_SAMPLE_CAPACITY + 1);

    StimulusScorer dark; Warm(dark, c);
    for (int i = 0; i < 120; ++i) dark.Evaluate(Sample(40), c);
    const float darkMedian = dark.LightBaselineMedian();
    float restoredScore = 0;
    for (int i = 0; i < 240; ++i) restoredScore = dark.Evaluate(Sample(), c).combined;
    assert(darkMedian == 100 && dark.LightBaselineMedian() == 100 && restoredScore == 0);

    StimulusScorer blind; float steadyHighScore = 0;
    for (int i = 0; i < 100; ++i) steadyHighScore = blind.Evaluate(Sample(1000, .2f), c).combined;
    assert(steadyHighScore == 0); // Constant high input cannot prove comfort.

    StimulusScorer boundary; Warm(boundary, c);
    float blockedScore = 0;
    for (int i = 0; i < 240; ++i) blockedScore = boundary.Evaluate(Sample(104), c).combined;
    assert(boundary.LightBaselineMedian() == 100 && fabsf(blockedScore - 1.1f) < .001f);
    for (const auto& entry : lightEntries) assert(entry.targetBrightness == c.preferredBrightness - 2);

    puts("{");
    puts("  \"kind\": \"synthetic_characterization_of_current_production_policies\",");
    puts("  \"clinical_validation\": false,");
    puts("  \"boundary_conditions\": \"constant 100 lx / RMS 0.01 baseline; other channel unchanged; stable outputs; 250 ms fresh inputs; lighting confirmed for 750 ms\",");
    puts("  \"light_entry_sweep_resolution_lux\": 0.01,");
    puts("  \"sound_to_light_sweep_resolution_rms\": 0.00001,");
    puts("  \"sound_to_fan_sweep_resolution_rms\": 0.0001,");
    puts("  \"sensitivity_rows\": [");
    const char* levels[] = {"low", "medium", "high"};
    for (int i = 0; i < 3; ++i) {
        printf("    {\"level\":\"%s\",\"light_gain\":%.2f,\"sound_gain\":%.2f,\"light_only_entry_delta_lux\":%.2f,\"light_entry_target_brightness_percent\":%u,\"sound_only_light_entry_delta_rms\":%.5f,\"fan_quiet_entry_delta_rms\":%.4f}%s\n",
            levels[i], lightGains[i], soundGains[i], lightEntries[i].delta, lightEntries[i].targetBrightness,
            soundEntries[i].delta, fanEntries[i], i == 2 ? "" : ",");
    }
    puts("  ],");
    printf("  \"first_score_using_full_baseline_sample_number\": %d,\n", firstReady);
    printf("  \"external_dark_30s_then_restore_60s\": {\"median_after_dark_lux\":%.1f,\"median_after_restore_lux\":%.1f,\"final_score\":%.1f,\"large_decrease_excluded\":true},\n", darkMedian, dark.LightBaselineMedian(), restoredScore);
    printf("  \"constant_high_input_since_boot\": {\"lux\":1000,\"rms\":0.2,\"final_score\":%.1f,\"comfort_is_not_inferred\":true},\n", steadyHighScore);
    printf("  \"small_sustained_step_100_to_104_lux\": {\"final_baseline_lux\":%.1f,\"final_score\":%.2f,\"observation_only_not_learned\":true}\n", boundary.LightBaselineMedian(), blockedScore);
    puts("}");
}
