#include "common/algorithm/output_aware_inputs.h"
#include "common/algorithm/stimulus_score.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    OutputAwareInputs observer;
    StimulusScorer scorer;
    AppConfig config;
    SensorSnapshot s = {150, {.01f, .02f, 0, 0, true}, true, 0, true, true, true};
    s.observedOutputs = {true, true, 100, 250, false, 255, 255, 255, true, 60, true, true};
    for (uint32_t t = 0; t <= 10000; t += 250) {
        s.observedAtMs = s.sound.timestampMs = t;
        scorer.Evaluate(observer.Observe(s), config);
    }
    assert(scorer.LightBaselineMedian() == 150);
    // A manual light command changes raw lux but not the learned external reference.
    s.observedOutputs.brightness = 25; s.lux = 112.5f;
    for (uint32_t t = 10250; t <= 40000; t += 250) {
        s.observedAtMs = s.sound.timestampMs = t;
        auto observed = observer.Observe(s);
        assert(observed.lux == 150);
        scorer.Evaluate(observed, config);
    }
    assert(scorer.LightBaselineMedian() == 150);
    // Real changes at fixed manual output remain observable and learnable.
    for (uint32_t t = 40250; t <= 50250; t += 250) {
        s.observedAtMs = s.sound.timestampMs = t; s.lux += .025f;
        scorer.Evaluate(observer.Observe(s), config);
    }
    assert(scorer.LightBaselineMedian() > 150);
    // Returning to the reference output compares against absolute readings again.
    s.observedOutputs.brightness = 100; s.lux = 151.025f;
    for (uint32_t t = 50500; t <= 51500; t += 250) {
        s.observedAtMs = s.sound.timestampMs = t;
        scorer.Evaluate(observer.Observe(s), config);
    }
    assert(scorer.LightBaselineMedian() > 150 && scorer.LightBaselineMedian() < 152);
    // Fan commands don't learn their own acoustic step; light learning remains independent.
    s.observedOutputs.fanPercent = 20; s.sound.energy = .002f; s.sound.peak = .004f;
    s.observedAtMs = s.sound.timestampMs = 51750;
    auto step = observer.Observe(s);
    assert(step.learnLight && !step.learnSound && fabsf(step.sound.energy - .01f) < .00001f);
    for (uint32_t t = 52000; t <= 53000; t += 250) {
        s.observedAtMs = s.sound.timestampMs = t;
        assert(fabsf(observer.Observe(s).sound.energy - .01f) < .00001f);
    }
    // Invalid and duplicate measurements cannot be counted as learning observations.
    auto duplicate = observer.Observe(s);
    assert(!duplicate.learnLight && !duplicate.learnSound);
    s.observedAtMs += 250; s.sound.timestampMs += 250; s.micValid = false;
    assert(!observer.Observe(s).learnSound);
    // A coincident external step is not identifiable by the uncalibrated fallback.
    OutputAwareSignal fallback;
    assert(fallback.Observe(150, 100, 0, true, true).value == 150);
    fallback.Observe(122.5f, 25, 250, true, true); // Own dimming -37.5 plus outside +10.
    auto ambiguous = fallback.Observe(122.5f, 25, 1000, true, true);
    assert(ambiguous.value == 150 && !ambiguous.learn);
    fallback.Observe(160, 100, 1250, true, true);
    assert(fallback.Observe(160, 100, 2000, true, true).value == 160);
    // Installation calibration can preserve an outside change during our ramp.
    OutputAwareInputs calibrated;
    LightSensorCalibration calibration{true, 50, 40, 30};
    calibrated.SetLightCalibration(calibration);
    s.micValid = true; s.observedOutputs.brightness = 100; s.observedOutputs.cct = 250;
    s.observedOutputs.lightSettled = true; s.lux = 150;
    s.observedAtMs = s.sound.timestampMs = 0;
    assert(calibrated.Observe(s).lux == 100);
    s.observedOutputs.brightness = 25; s.observedOutputs.lightSettled = false;
    s.observedAtMs = s.sound.timestampMs = 250; s.lux = 122.5f;
    auto outside = calibrated.Observe(s);
    assert(outside.learnLight && outside.lux == 110);
    // Sensor reads that span two actual output states cannot use a single correction.
    s.observedOutputs.lightObservationConsistent = false;
    s.observedAtMs = s.sound.timestampMs = 500;
    assert(!calibrated.Observe(s).learnLight);
    float ambient;
    s.observedOutputs.cct = 333; s.observedOutputs.brightness = 50;
    assert(calibration.Correct(120, s.observedOutputs, ambient) && ambient == 100);
    s.observedOutputs.cct = 454;
    assert(calibration.Correct(115, s.observedOutputs, ambient) && ambient == 100);
    assert(!calibration.Correct(NAN, s.observedOutputs, ambient));
    s.observedOutputs.rgb = true;
    assert(!calibration.Correct(115, s.observedOutputs, ambient));
    OutputAwareSignal wrapping;
    wrapping.Observe(100, 1, UINT32_MAX - 100, true, true);
    assert(wrapping.Observe(101, 1, 149, true, true).value == 101);
    puts("own light/fan transitions excluded, external changes learned and fresh samples enforced passed");
}
