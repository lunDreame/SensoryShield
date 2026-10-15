#include "common/algorithm/algorithm.h"
#include "demo/environment_samples.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
uint32_t testUptimeMs = 1000;
struct Rig {
    Algorithm& a = Algorithm::Instance();
    AppConfig c;
    int brightness = 100, cct = 250, fan = 100;
    float extraLux = 0, extraSound = 0;
    bool automatic = true, micValid = true, pir = true, fanValid = true, staleMic = false;
    uint32_t elapsed = 0;
    SensorSnapshot s{};
    StimulusScore score{};
    ControlTarget target = TimedReliefController::Normal(c);
    Rig() {
        a.Initialize(c);
    }
    void Run(uint32_t duration) {
        const auto end = elapsed + duration;
        while (elapsed < end) {
            testUptimeMs = 1000 + elapsed;
            if (elapsed % SENSOR_PERIOD_MS == 0) {
                const float rms = .01f + .00005f * fan + extraSound;
                s = {100 + .5f * brightness + extraLux,
                     {rms, rms, 0, testUptimeMs, micValid},
                     pir,
                     testUptimeMs,
                     true,
                     micValid,
                     true};
                s.observedAtMs = testUptimeMs;
                if (staleMic) s.sound.timestampMs = testUptimeMs - 1500;
                s.observedOutputs = {true,
                                     brightness > 0,
                                     static_cast<uint8_t>(brightness),
                                     static_cast<uint16_t>(cct),
                                     false,
                                     255,
                                     255,
                                     255,
                                     fan > 0,
                                     static_cast<uint8_t>(fan),
                                     brightness == target.brightnessPercent && cct == target.cctMireds,
                                     fan == target.fanPercent};
                s.observedOutputs.fanOutputValid = fanValid;
            }
            if (elapsed % ALGORITHM_PERIOD_MS == 0) {
                score = a.EvaluateEnvironment(s, automatic);
                if (!automatic && a.ChildHoldComplete(testUptimeMs))
                    automatic = true;
                target = automatic ? a.Evaluate(s, score) : a.EvaluateChild(s, score);
            }
            brightness += std::clamp(int(target.brightnessPercent) - brightness, -2, 2);
            cct += std::clamp(int(target.cctMireds) - cct, -8, 8);
            fan += std::clamp(int(target.fanPercent) - fan, -2, 2);
            assert(brightness >= 0 && brightness <= 100 && fan >= 0 && fan <= 100);
            elapsed += ACTUATOR_RAMP_MS;
        }
    }
    bool Normal() const {
        return brightness == 100 && cct == 250 && fan == 100;
    }
    bool Relief() const {
        return brightness == 30 && cct == 454 && fan == c.reliefFanPercent;
    }
};
int main() {
    // Reproduce the reported AUTO/high-score/white-light stall, then restore
    // fresh capture. Every selected relief tone must reach the output with fan OFF.
    for (uint16_t tone : {250, 333, 454}) {
        Rig recovered;
        recovered.c.preferredCCTMireds=tone;
        recovered.a.SetConfig(recovered.c);
        recovered.Run(15000);
        recovered.staleMic=true; recovered.extraLux=50;
        recovered.Run(12000);
        assert(recovered.score.combined>=1.4f && recovered.brightness==100);
        assert(!recovered.a.ControlInputsSettled());
        recovered.staleMic=false; recovered.Run(15000);
        assert(recovered.brightness==30 && recovered.cct==tone && recovered.fan==0);
        // A new tone during relief is also applied, without changing brightness.
        recovered.c.preferredCCTMireds=tone==454 ? 333 : 454;
        recovered.a.SetConfig(recovered.c); recovered.Run(6500);
        assert(recovered.cct==recovered.c.preferredCCTMireds && recovered.brightness==30 && recovered.fan==0);
        recovered.extraLux=0; recovered.Run(16000);
        assert(recovered.Normal());
    }
    assert(!RehearsalBaseline::Enabled && !RehearsalBaseline::Samples.Valid());
    Rig r;
    r.Run(15000);
    assert(r.score.baselineReady && r.Normal() && !r.a.BaselineFrozen());
    const float base = r.a.LightBaselineMedian(), sound = r.a.SoundBaselineMedian();
    // Repeated short flashes do not accumulate confirmation across quiet gaps.
    for (int i = 0; i < 3; ++i) {
        r.extraLux = 50;
        r.Run(2500);
        assert(r.Normal());
        r.extraLux = 0;
        r.Run(1000);
    }
    for (int input = 0; input < 3; ++input) {
        r.extraLux = input != 1 ? 50 : 0;
        r.extraSound = input != 0 ? .06f : 0;
        r.Run(3000);
        assert(r.Normal());
        r.Run(1500);
        assert(r.brightness < 100 && r.brightness > 30 && r.fan < 100 && r.fan > r.c.reliefFanPercent && r.cct > 250 && r.cct < 454);
        r.Run(4500);
        assert(r.Relief());
        r.Run(10000);
        assert(r.Relief()); // High input never expires on the 5-second timer.
        assert(r.a.LightBaselineMedian() == base && r.a.SoundBaselineMedian() == sound);
        r.extraLux = r.extraSound = 0;
        r.Run(3000);
        assert(r.Relief());
        r.Run(3000);
        assert(!r.Normal() && r.brightness > 30);
        r.Run(4000);
        assert(r.Normal());
        r.Run(3000);
    }
    // Hold starts on actual arrival; recovery confirmation overlaps that hold.
    r.a.StartChild(r.target, TimedReliefController::ChildTarget(ComfortAction::Bright, r.target, r.c), testUptimeMs);
    r.automatic = false;
    const auto childStart = r.elapsed;
    r.Run(4500);
    assert(r.automatic == false && r.brightness > 30 && r.a.ChildRemainingSeconds(testUptimeMs) == 5);
    r.Run(1500);
    assert(r.brightness == 30 && r.fan == 100 && !r.automatic);
    r.Run(3500);
    assert(!r.automatic && r.brightness == 30);
    r.Run(2000);
    assert(r.automatic && r.brightness > 30 && r.brightness < 100);
    r.Run(5000);
    assert(r.Normal());
    printf("child adjustment + arrival hold + conditional recovery completed in %u ms\n", r.elapsed - childStart);
    assert(r.a.LightBaselineMedian() == base && r.a.SoundBaselineMedian() == sound);
    // Input fault cannot cause recovery; output fault cannot start a hold timer.
    r.extraSound = .06f;
    r.Run(10000);
    assert(r.Relief());
    r.extraSound = 0;
    r.micValid = false;
    r.Run(15000);
    assert(r.Relief());
    r.micValid = true;
    r.Run(12000);
    assert(r.Normal());
    r.a.StartChild(r.target, TimedReliefController::ChildTarget(ComfortAction::Loud, r.target, r.c), testUptimeMs);
    r.automatic = false;
    r.fanValid = false;
    r.Run(15000);
    assert(!r.automatic);
    r.fanValid = true;
    r.Run(4000);
    assert(!r.automatic);
    r.Run(9000);
    assert(r.automatic && r.Normal());
    // Recovery pauses on renewed input and does not cycle forever at a constant high source.
    r.extraLux = 50;
    r.Run(10000);
    assert(r.Relief());
    r.extraLux = 0;
    r.Run(6000);
    assert(r.brightness > 30 && r.brightness < 100);
    r.extraLux = 50;
    r.Run(15000);
    assert(r.Relief());
    r.Run(30000);
    assert(r.Relief());
    r.extraLux = 0;
    r.Run(15000);
    assert(r.Normal());
    // Empty/invalid/mismatched samples are rejected, no invented fixed baseline.
    InitialEnvironment initial{};
    assert(!r.a.InstallRehearsalBaseline(initial));
    initial.count = 31;
    for (int i = 0; i < 31; ++i) {
        initial.lux[i] = 150 + (i % 3 - 1);
        initial.sound[i] = .015f;
    }
    auto invalid = initial;
    invalid.sound[0] = NAN;
    assert(!r.a.InstallRehearsalBaseline(invalid));
    invalid = initial;
    invalid.lux[0] = -1;
    assert(!r.a.InstallRehearsalBaseline(invalid));
    assert(r.a.InstallRehearsalBaseline(initial) && r.a.BaselineFrozen());
    r.extraLux = 1;
    r.Run(20000);
    assert(r.a.LightBaselineMedian() == 150);
    r.extraLux = 50;
    r.Run(10000);
    assert(r.Relief());
    r.extraLux = 0;
    r.Run(15000);
    assert(r.Normal());
    assert(r.a.BaselineFrozen() && r.a.LightBaselineMedian() == 150);
    r.c.preferredBrightness = 60;
    r.a.SetConfig(r.c);
    assert(!r.a.BaselineFrozen() && !r.a.InstallRehearsalBaseline(initial));
    // Guardian bounds and complaints must not turn an already-off output on.
    auto off = TimedReliefController::Normal(r.c);
    off.lightOn = off.fanOn = false;
    off.brightnessPercent = off.fanPercent = 0;
    auto t = TimedReliefController::ChildTarget(ComfortAction::Bright, off, r.c);
    assert(!t.lightOn && !t.fanOn);
    t = TimedReliefController::ChildTarget(ComfortAction::Dark, off, r.c);
    assert(t.lightOn && t.brightnessPercent == 30);
    r.c.fanMaxPercent = 10;
    t = TimedReliefController::ChildTarget(ComfortAction::Quiet, off, r.c);
    assert(!t.fanOn);
    // Direct threshold boundaries: a middle-band recovery input holds, not re-relief.
    {
        TimedReliefController controller;
        AppConfig config;
        uint32_t now = 0xfffff000U; // Confirmation and ramp clocks also cross uptime wrap.
        auto output = TimedReliefController::Normal(config);
        SensorSnapshot snapshot{};
        auto tick = [&](float level) {
            now += 250;
            snapshot = {100, {.01f, .01f, 0, now, true}, true, now, true, true, true};
            snapshot.observedAtMs = now;
            snapshot.observedOutputs = {true,
                                        output.lightOn,
                                        output.brightnessPercent,
                                        output.cctMireds,
                                        false,
                                        255,
                                        255,
                                        255,
                                        output.fanOn,
                                        output.fanPercent,
                                        true,
                                        true};
            output = controller.Evaluate(snapshot, config, level, now);
        };
        tick(0);
        for (int i = 0; i < 12; ++i) {
            tick(1.4f);
            assert(controller.IsNormal());
        }
        tick(1.4f);
        assert(controller.CurrentPhase() == TimedReliefController::Phase::Adjust);
        for (int i = 0; i < 25; ++i)
            tick(2);
        assert(output.brightnessPercent == 30 && output.fanPercent == config.reliefFanPercent);
        for (int i = 0; i < 18; ++i)
            tick(.7f);
        for (int i = 0; i < 4; ++i)
            tick(.7f);
        assert(output.brightnessPercent > 30 && output.brightnessPercent < 100);
        const auto held = output;
        for (int i = 0; i < 20; ++i) {
            tick(1.0f);
            assert(!(output != held));
        }
        assert(controller.CurrentPhase() == TimedReliefController::Phase::Recover);
        // Repeating the same sensor timestamp is not a stable 3-second observation.
        for (int i = 0; i < 20; ++i) {
            now += 250;
            output = controller.Evaluate(snapshot, config, 0, now);
            assert(!(output != held));
        }
        for (int i = 0; i < 12; ++i) {
            tick(.7f);
            assert(!(output != held));
        }
        for (int i = 0; i < 35; ++i)
            tick(0);
        assert(controller.IsNormal() && output.brightnessPercent == 100 && output.fanPercent == 100);
    }
    puts("real algorithm: live/frozen learning, separate light/sound/joint input, 3s confirmation, 5s ramps, arrival "
         "hold, conditional child AUTO, fault/recurrence guards passed");
}
