#include "common/algorithm/environment_measurement.h"
#include "common/algorithm/stimulus_score.h"
#include "common/algorithm/sound_fan.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    EnvironmentMeasurement measurement;
    SensorSnapshot input{100, {.01f, .01f, 0, 0, true}, true, 0, true, true, true};
    measurement.Start(0, 60, 333);
    measurement.Observe(input, false, 0);
    assert(measurement.Data().count == 0);
    measurement.Observe(input, true, 0);
    measurement.Observe(input, true, 100);
    assert(measurement.Data().count == 1); // Repeated microphone sample is not a new observation.
    input.lux = std::numeric_limits<float>::quiet_NaN();
    input.sound.timestampMs = 200;
    measurement.Observe(input, true, 200);
    assert(measurement.Data().count == 1);
    input.lux = 100;
    measurement.Observe(input, false, 200);
    assert(measurement.Data().count == 0); // A changed light must settle again.
    for (uint32_t i = 0; i < ENVIRONMENT_SAMPLE_CAPACITY; ++i) {
        input.sound.timestampMs = 400 + i * 200;
        input.lux = 100 + static_cast<int>(i % 3) - 1;
        measurement.Observe(input, true, input.sound.timestampMs);
    }
    assert(measurement.State() == MeasurementState::Ready && measurement.Data().Valid());
    assert(measurement.Matches(60, 333) && !measurement.Matches(70, 333));
    StimulusScorer scorer;
    scorer.SeedEnvironment(measurement.Data());
    AppConfig config;
    input.lux = 100;
    auto normal = scorer.Evaluate(input, config);
    assert(normal.baselineReady && normal.combined == 0);
    input.lux = 150;
    assert(scorer.Evaluate(input, config).combined == STIMULUS_MAX_SCORE);
    input.lux = 100;
    assert(scorer.Evaluate(input, config).combined == 0);
    // Recorded observations start the rolling model; they do not freeze it.
    input.lux = 101;
    for (int i = 0; i < 31; ++i) scorer.Evaluate(input, config);
    assert(scorer.LightBaselineMedian() == 101);
    SoundFanController fan;
    fan.SeedEnvironment(measurement.Data());
    assert(fan.Evaluate(input, config, input.sound.timestampMs) == AUTO_FAN_QUIET_PERCENT);
    input.sound.timestampMs += ALGORITHM_PERIOD_MS;
    assert(fan.Evaluate(input, config, input.sound.timestampMs) > AUTO_FAN_QUIET_PERCENT);
    measurement.Invalidate();
    assert(measurement.State() == MeasurementState::Ready && measurement.Data().Valid()); // Experience commands preserve the pre-trial record.
    assert(measurement.Data().lux[0] == 99);
    measurement.Start(0, 60, 333);
    input.micValid = false;
    measurement.Observe(input, true, EnvironmentMeasurement::TimeoutMs);
    assert(measurement.State() == MeasurementState::Error);
    measurement.Cancel();
    assert(measurement.State() == MeasurementState::Idle);
    puts("fresh settled observations, failures, seeded scoring/fan and continued rolling learning passed");
}
