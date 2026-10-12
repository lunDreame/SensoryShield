#include "fan_probe.h"
#include "common/algorithm/algorithm.h"
#include <cassert>
#include <cmath>
#include <cstdio>
uint32_t testUptimeMs = 0;
SensorSnapshot sample{100,{.01f,.01f,0,0,true},true,1,true,true,true};
int main() {
    auto& a = Algorithm::Instance(); AppConfig config; config.minBrightness=1; config.maxBrightness=100; config.preferredBrightness=100; config.reliefHoldMs=1500; config.occupancyAutomationEnabled=true;
    auto tick = [&](float lux, bool automatic = true) {
        testUptimeMs += 250; sample.lux=lux; sample.sound.timestampMs=testUptimeMs;
        const auto score=a.EvaluateEnvironment(sample,automatic);
        if (automatic) a.Evaluate(sample,score);
        return score;
    };
    a.Initialize(config);
    for (int i=0;i<40;++i) tick(100);
    for (int i=0;i<40;++i) tick(102);
    assert(a.LightBaselineMedian()==102); // Within the learning boundary is ordinary admission.
    for (int i=0;i<100;++i) tick(105,false);
    assert(a.LightBaselineMedian()==102); // Manual/override/safe score but never learn.
    a.Initialize(config);
    for (int i=0;i<40;++i) tick(100);
    for (int i=0;i<20;++i) tick(120);
    assert(a.LightBaselineMedian()==100);
    for (int i=0;i<30;++i) tick(101);
    assert(a.LightBaselineMedian()==100); // Low raw score while recovering still cannot enter learning.
    for (int i=0;i<300;++i) tick(101);
    assert(a.LightBaselineMedian()==101); // Learning resumes at ordinary settled output.

    // A provisional baseline must not disable fan response before 31 samples.
    a.Initialize(config); sample.lux=100; sample.sound.energy=sample.sound.peak=.01f;
    for(int i=0;i<4;++i) tick(100);
    ControlTarget early{};
    for(int i=0;i<5;++i) {
        testUptimeMs+=250; sample.sound={.10f,.10f,0,testUptimeMs,true};
        auto score=a.EvaluateEnvironment(sample); assert(!score.soundBaselineReady);
        early=a.Evaluate(sample,score);
    }
    assert(early.fanPercent<config.preferredFanPercent);
    sample.sound.energy=sample.sound.peak=.01f;

    SensoryReliefController light; SensorSnapshot s=sample; s.lux=100; s.sound.energy=.01f;
    uint32_t now=0;
    auto step=[&](float score) {now+=250; s.sound.timestampMs=now; auto lightOnly=config; lightOnly.preferredFanPercent=0; return light.Evaluate(s,lightOnly,score,now);};
    step(0);
    for(int i=0;i<5;++i) step(4);
    for(int i=0;i<30;++i) step(0);
    auto before=step(0); assert(before.brightnessPercent>config.minBrightness && !light.IsNormal());
    for(int i=0;i<40;++i) {
        auto held=step(1); assert(held.brightnessPercent==before.brightnessPercent && held.cctMireds==before.cctMireds);
    }
    // A paused recovery needs the full low confirmation, not just one low sample.
    for(int i=0;i<9;++i) assert(step(0).brightnessPercent==before.brightnessPercent);
    assert(step(1).brightnessPercent==before.brightnessPercent);
    // A short high spike must not restart relief or change the paused output.
    assert(step(2).brightnessPercent==before.brightnessPercent);
    assert(step(1).brightnessPercent==before.brightnessPercent);
    for(int i=0;i<5;++i) step(2);
    auto blocked=step(0);
    assert(blocked.brightnessPercent<=before.brightnessPercent);
    for(int i=0;i<120;++i) assert(step(0).brightnessPercent==blocked.brightnessPercent);
    s.lux=97; // A fresh improvement at the same held output permits a new low confirmation.
    for(int i=0;i<200;++i) step(0);
    assert(light.IsNormal());

    FanProbe fan; now=0; s.lux=100;
    auto fanStep=[&](float score) {now+=250; s.sound.timestampMs=now; return fan.Evaluate(s,config,{0,score,score,true,true},now);};
    for(int i=0;i<5;++i) fanStep(0);
    for(int i=0;i<40;++i) fanStep(4);
    for(int i=0;i<20;++i) fanStep(0);
    auto speed=fanStep(0); assert(speed>config.reliefFanPercent && !fan.IsNormal());
    for(int i=0;i<20;++i) assert(fanStep(1)==speed);
    for(int i=0;i<9;++i) assert(fanStep(0)==speed);
    assert(fanStep(1)==speed);
    for(int i=0;i<5;++i) fanStep(2);
    auto held=fanStep(0);
    for(int i=0;i<120;++i) assert(fanStep(0)==held);
    s.sound.energy=.007f; s.sound.peak=.01f;
    for(int i=0;i<100;++i) fanStep(0);
    assert(fan.IsNormal() && fanStep(0)==config.preferredFanPercent);
    puts("ordinary-only admission, all non-auto modes frozen, middle-band hold and failed-recovery latch passed");
}
