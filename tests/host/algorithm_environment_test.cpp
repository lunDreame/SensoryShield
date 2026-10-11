#include "common/algorithm/algorithm.h"
#include <cassert>
#include <cstdio>
uint32_t testUptimeMs = 1000;
void coupledOutputs(float lampCoupling, float fanCoupling) {
    auto& a = Algorithm::Instance();
    AppConfig config; config.preferredCCTMireds = 417;
    a.Initialize(config);
    uint8_t brightness = 100, fan = 20;
    uint16_t cct = config.minCCTMireds;
    ControlTarget target{true, brightness, cct, false, 255, 255, 255, true, fan, 0};
    SensorSnapshot s = {0, {}, true, 1, true, true, true};
    bool dimmed = false, quieted = false, restored = false;
    for (uint32_t elapsed = 0; elapsed <= 180000; elapsed += ACTUATOR_RAMP_MS) {
        testUptimeMs = 1000 + elapsed;
        const bool noise = elapsed >= 40000 && elapsed < 70000;
        if (elapsed % SENSOR_PERIOD_MS == 0) {
            s.lux = 100 + (noise ? 10 : 0) + lampCoupling * brightness;
            const float energy = .01f + (noise ? .08f : 0) + fanCoupling * fan;
            s.sound = {energy, energy, 0, testUptimeMs, true};
            s.observedAtMs = testUptimeMs;
            s.observedOutputs = {true, brightness > 0, brightness, cct, false, 255, 255, 255,
                fan > 0, fan, brightness == target.brightnessPercent && cct == target.cctMireds,
                fan == target.fanPercent};
        }
        if (elapsed % ALGORITHM_PERIOD_MS == 0) {
            auto score = a.EvaluateEnvironment(s);
            target = a.Evaluate(s, score);
            if (elapsed > 55000 && elapsed < 70000) {
                dimmed |= target.brightnessPercent < 100 && target.cctMireds == 417;
                quieted |= target.fanPercent == 20;
            }
            if (elapsed >= 170000) {
                assert(target.brightnessPercent == 100 && target.cctMireds == config.minCCTMireds);
                assert(target.fanPercent == config.preferredFanPercent);
                restored = true;
            }
        }
        auto move = [](int current, int desired, int step) {
            return current + (desired > current ? (desired - current > step ? step : desired - current)
                : -(current - desired > step ? step : current - desired));
        };
        brightness = move(brightness, target.brightnessPercent, 2);
        cct = move(cct, target.cctMireds, 8);
        fan = move(fan, target.fanPercent, 2);
    }
    assert(dimmed && quieted && restored);
}
int main() {
    auto& a = Algorithm::Instance();
    AppConfig config; config.minBrightness = 20; config.maxBrightness = 80;
    a.Initialize(config);
    SensorSnapshot s = {100, {.01f, .01f, 0, testUptimeMs, true}, true, testUptimeMs, true, true, true};
    for (int i = 0; i < ENVIRONMENT_SAMPLE_CAPACITY; ++i) {
        const auto initial = a.EvaluateEnvironment(s);
        assert(initial.combined == 0 && !initial.baselineReady);
    }
    assert(a.EvaluateEnvironment(s).baselineReady);
    s.lux = 120;
    for (int i = 0; i < 5; ++i) {
        testUptimeMs += 250; s.sound.timestampMs = testUptimeMs;
        const auto score = a.EvaluateEnvironment(s);
        assert(score.combined == STIMULUS_MAX_SCORE);
        a.Evaluate(s, score);
    }
    auto relief = a.Evaluate(s, a.EvaluateEnvironment(s));
    assert(relief.brightnessPercent == 20 && relief.cctMireds == config.preferredCCTMireds);
    // Manual output priority remains in System; resetting auto does not reset learned input.
    a.ResetAutomaticLight();
    s.lux = 100;
    assert(a.Evaluate(s, a.EvaluateEnvironment(s)).brightnessPercent == 80);
    s.occupied = false; s.lastMotionMs = 0;
    auto absent = a.Evaluate(s, a.EvaluateEnvironment(s));
    assert(!absent.lightOn && !absent.fanOn);
    a.Initialize(config);
    assert(a.EvaluateEnvironment(s).combined == 0);
    s.lux = 140;
    assert(a.EvaluateEnvironment(s).combined > 0); // Provisional comparison starts at first sample.
    coupledOutputs(0, 0);
    coupledOutputs(.5f, .00005f);
    coupledOutputs(2, .0001f);
    puts("automatic live baseline, provisional scoring, mode reset and unchanged PIR hold passed");
}
