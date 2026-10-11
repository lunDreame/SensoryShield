#include "common/algorithm/algorithm.h"
#include <cassert>
#include <cstdio>

uint32_t testUptimeMs = 1000;

int main() {
    auto& algorithm = Algorithm::Instance();
    AppConfig config;
    config.minBrightness = 20;
    config.maxBrightness = 80;
    algorithm.Initialize(config);
    SensorSnapshot snapshot = {100.0f, {0.01f, 0.01f, 0.0f, testUptimeMs, true},
                               true, testUptimeMs, true, true, true};

    // Observation alone (manual/override/safe) establishes the baseline.
    for (int i = 0; i < 5; ++i) {
        const auto warming = algorithm.EvaluateEnvironment(snapshot);
        assert(warming.combined == 0.0f);
        assert(!warming.baselineReady);
    }
    snapshot.lux = 120.0f;
    const auto increased = algorithm.EvaluateEnvironment(snapshot);
    assert(increased.combined == STIMULUS_MAX_SCORE);
    assert(increased.baselineReady);
    snapshot.micValid = false;
    assert(!algorithm.EvaluateEnvironment(snapshot).baselineReady);
    snapshot.micValid = true;
    snapshot.lux = 100.0f;
    assert(algorithm.EvaluateEnvironment(snapshot).combined == 0.0f);

    // Returning to auto uses the observed score; observation has not started
    // or warmed the automatic fan controller in the background.
    snapshot.lux = 120.0f;
    const auto stimulus = algorithm.EvaluateEnvironment(snapshot);
    const auto target = algorithm.Evaluate(snapshot, stimulus);
    assert(target.sensoryScore == stimulus.combined);
    assert(target.lightOn && target.brightnessPercent == config.minBrightness);
    assert(target.fanPercent == AUTO_FAN_QUIET_PERCENT);

    // Unoccupied automatic outputs stop even though environment scoring continues.
    snapshot.occupied = false;
    snapshot.lastMotionMs = 0;
    const auto absentScore = algorithm.EvaluateEnvironment(snapshot);
    const auto absent = algorithm.Evaluate(snapshot, absentScore);
    assert(absent.sensoryScore == STIMULUS_MAX_SCORE);
    assert(!absent.lightOn && !absent.fanOn);
    algorithm.Initialize(config);
    assert(!algorithm.EvaluateEnvironment(snapshot).baselineReady);
    snapshot.lux = 140.0f;
    const auto provisional = algorithm.EvaluateEnvironment(snapshot);
    assert(!provisional.baselineReady);
    assert(provisional.combined > 0.0f);
    puts("continuous scoring, provisional scoring and baseline readiness passed");
}
