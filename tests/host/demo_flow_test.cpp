#include "common/algorithm/algorithm.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

uint32_t testUptimeMs=1000;
int main() {
    auto& a=Algorithm::Instance(); AppConfig c;
    assert(c.preferredBrightness==60 && c.minBrightness==30 && c.maxBrightness==80);
    assert(c.normalCCTMireds==250 && c.preferredCCTMireds==417);
    assert(c.preferredFanPercent==60 && c.reliefFanPercent==20);
    assert(c.reliefHoldMs==3000 && c.recoverConfirmMs==3000 && !c.occupancyAutomationEnabled);
    a.Initialize(c);
    int brightness=60,cct=250,fan=60; uint32_t elapsed=0;
    float extraLux=0,extraSound=0;
    bool automatic=true;
    SensorSnapshot s{}; StimulusScore score{};
    ControlTarget target{true,60,250,false,255,255,255,true,60,0};
    auto run=[&](uint32_t ms) {
        const auto end=elapsed+ms;
        while(elapsed<end) {
            testUptimeMs=1000+elapsed;
            if(elapsed%SENSOR_PERIOD_MS==0) {
                const float rms=.01f+.00005f*fan+extraSound;
                s={100+.5f*brightness+extraLux,{rms,rms,0,testUptimeMs,true},false,0,true,true,true};
                s.observedAtMs=testUptimeMs;
                s.observedOutputs={true,brightness>0,static_cast<uint8_t>(brightness),static_cast<uint16_t>(cct),false,255,255,255,
                    fan>0,static_cast<uint8_t>(fan),brightness==target.brightnessPercent && cct==target.cctMireds,fan==target.fanPercent};
            }
            if(elapsed%ALGORITHM_PERIOD_MS==0) {
                score=a.EvaluateEnvironment(s,automatic);
                if(automatic) target=a.Evaluate(s,score);
            }
            brightness+=std::clamp(int(target.brightnessPercent)-brightness,-2,2);
            cct+=std::clamp(int(target.cctMireds)-cct,-8,8);
            fan+=std::clamp(int(target.fanPercent)-fan,-2,2);
            assert(brightness>=0 && brightness<=80 && fan>=0 && fan<=100);
            elapsed+=ACTUATOR_RAMP_MS;
        }
    };
    run(30000); assert(score.baselineReady && brightness==60 && fan==60);
    const auto baseline=a.LightBaselineMedian();
    extraLux=20; run(500); assert(brightness==60); // A transient does not confirm entry.
    extraLux=0; run(5000); assert(brightness==60);
    extraLux=20; run(5000); assert(brightness==30 && cct==417 && fan<60);
    assert(a.LightBaselineMedian()==baseline);
    extraLux=0; run(5000); assert(brightness<60); // 3s hold + low confirmation precede gradual return.
    uint32_t lightStart=elapsed;
    while(brightness!=60 || cct!=250 || fan!=60) { run(1000); assert(elapsed-lightStart<120000); }
    printf("demo light restoration after low confirmation: %u ms\n",elapsed-lightStart+5000);
    run(10000); assert(a.LightBaselineMedian()==baseline);
    extraSound=.06f; run(35000); assert(fan==20 && brightness==30 && cct==417);
    extraSound=0; uint32_t fanStart=elapsed;
    while(fan!=60 || brightness!=60 || cct!=250) { run(1000); assert(elapsed-fanStart<120000); }
    printf("demo coupled sound restoration: %u ms\n",elapsed-fanStart);
    run(10000);
    automatic=false; target.brightnessPercent=40; target.fanPercent=30;
    const auto oldLux=a.LightBaselineMedian(),oldSound=a.SoundBaselineMedian();
    run(10000); assert(a.LightBaselineMedian()==oldLux && a.SoundBaselineMedian()==oldSound);
    automatic=true; a.ResetAutomaticLight(); run(30000);
    assert(brightness==60 && fan==60 && score.baselineReady);
    // Default demo ignores absent motion only for termination, not the raw telemetry.
    assert(!s.occupied && brightness>0 && fan>0);
    puts("fresh demo defaults, transient/sustained light, coupled sound, complete recovery and manual learning exclusion passed");
}
