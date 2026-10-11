#include "common/algorithm/algorithm.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>

uint32_t testUptimeMs=1000;
struct Room {
    Algorithm& a=Algorithm::Instance(); AppConfig c;
    uint32_t elapsed=0;
    float ambient=100, coupling=.5f, toneCoupling=0, rms=.01f;
    bool automatic=true, inputValid=true, occupied=true;
    int brightness=40, cct=250, fan=60;
    SensorSnapshot s{}; StimulusScore score{}; ControlTarget target{};
    Room() {
        c.preferredBrightness=40; c.occupancyAutomationEnabled=false;
        a.Initialize(c);
        target={true,40,250,false,255,255,255,true,60,0};
    }
    void run(uint32_t duration) {
        const uint32_t end=elapsed+duration;
        while(elapsed<end) {
            testUptimeMs=1000+elapsed;
            if(elapsed%SENSOR_PERIOD_MS==0) {
                s={ambient+coupling*brightness+toneCoupling*(cct-250),
                    {rms,rms,0,testUptimeMs,true},occupied,occupied?testUptimeMs:0,
                    inputValid,true,true};
                s.observedAtMs=testUptimeMs;
                s.observedOutputs={true,brightness>0,static_cast<uint8_t>(brightness),static_cast<uint16_t>(cct),
                    false,255,255,255,fan>0,static_cast<uint8_t>(fan),
                    brightness==target.brightnessPercent && cct==target.cctMireds,fan==target.fanPercent};
            }
            if(elapsed%ALGORITHM_PERIOD_MS==0) {
                score=a.EvaluateEnvironment(s,automatic);
                if(automatic) target=a.Evaluate(s,score);
            }
            brightness+=std::clamp(int(target.brightnessPercent)-brightness,-2,2);
            cct+=std::clamp(int(target.cctMireds)-cct,-8,8);
            fan+=std::clamp(int(target.fanPercent)-fan,-2,2);
            elapsed+=ACTUATOR_RAMP_MS;
        }
    }
    void save() { a.SetConfig(c); }
};
int main() {
    {
        Room r; r.run(10000); assert(r.a.LightBaselineMedian()==120);
        const float sound=r.a.SoundBaselineMedian();
        r.c.preferredBrightness=80; r.save();
        assert(r.a.LightRebasePending() && r.a.LightBaselineMedian()==120);
        r.run(500); // Ramping/settling cannot replace the old context.
        assert(r.a.LightRebasePending() && r.a.LightBaselineMedian()==120);
        r.run(60000);
        assert(!r.a.LightRebasePending() && r.brightness==80 && r.a.LightBaselineMedian()==140);
        assert(r.score.combined==0 && r.a.SoundBaselineMedian()==sound);
        // Small changes resume learning; new large changes still trigger relief.
        r.ambient+=2; r.run(10000); assert(r.a.LightBaselineMedian()==142);
        r.ambient+=20; r.run(5000); assert(r.brightness<80 && r.a.LightBaselineMedian()==142);
    }
    {
        Room r; r.toneCoupling=.05f; r.run(10000);
        r.c.normalCCTMireds=400; r.save(); r.run(10000);
        assert(r.cct==400 && !r.a.LightRebasePending());
        assert(std::fabs(r.a.LightBaselineMedian()-127.5f)<.001f);
        // Re-saving, sensitivity, PIR and relief-only edits keep the baseline.
        r.c.preferredCCTMireds=417; r.c.lightWeight=.8f; r.c.occupancyAutomationEnabled=true;
        r.save(); assert(!r.a.LightRebasePending());
        assert(std::fabs(r.a.LightBaselineMedian()-127.5f)<.001f);
        // A cap change that changes the effective normal output also rebases.
        r.c.maxBrightness=30; r.save(); r.run(10000);
        assert(r.brightness==30 && std::fabs(r.a.LightBaselineMedian()-122.5f)<.001f);
    }
    {
        Room r; r.run(10000);
        r.automatic=false; r.target.brightnessPercent=10; r.run(10000);
        assert(r.a.LightBaselineMedian()==120 && !r.a.LightRebasePending());
        r.c.preferredBrightness=80; r.save(); r.run(10000);
        assert(r.a.LightRebasePending() && r.a.LightBaselineMedian()==120);
        // Invalid observations cannot establish a new baseline, either.
        r.inputValid=false; r.automatic=true; r.a.ResetAutomaticLight(); r.run(5000);
        assert(r.a.LightRebasePending() && r.a.LightBaselineMedian()==120);
        r.inputValid=true; r.run(15000);
        assert(r.brightness==80 && r.a.LightBaselineMedian()==140 && !r.a.LightRebasePending());
    }
    {
        Room r; r.run(10000);
        r.rms=.10f; r.run(15000);
        r.c.preferredBrightness=80; r.save();
        r.run(15000); // Never discard the previously learned sound protection.
        assert(r.score.sound>1.4f && r.brightness<80 && r.fan<60);
        assert(r.a.LightRebasePending() && r.a.SoundBaselineMedian()==.01f);
        r.rms=.01f; r.run(120000);
        assert(!r.a.LightRebasePending() && r.brightness==80 && r.a.LightBaselineMedian()==140);
        assert(r.a.SoundBaselineMedian()==.01f);
    }
    {
        // A recovery blocked under the old default must not deadlock a new default.
        Room r; r.coupling=10; r.run(10000);
        r.ambient+=20; r.run(90000);
        const int held=r.brightness; assert(held<40);
        r.run(10000); assert(r.brightness==held);
        r.c.preferredBrightness=50; r.save(); r.run(120000);
        assert(!r.a.LightRebasePending() && r.brightness==50 && r.a.LightBaselineMedian()==620);
    }
    {
        Room r; r.c.occupancyAutomationEnabled=true; r.save(); r.run(10000);
        r.occupied=false; r.run(40000);
        r.c.preferredBrightness=80; r.save(); r.run(10000);
        assert(r.brightness==0 && r.a.LightRebasePending());
        r.occupied=true; r.run(15000);
        assert(r.brightness==80 && !r.a.LightRebasePending() && r.a.LightBaselineMedian()==140);
    }
    {
        // Changing the requested default again during its ramp uses only the
        // final observed output; no intermediate 80% context is learned.
        Room r; r.run(10000);
        r.c.preferredBrightness=80; r.save(); r.run(250);
        r.c.preferredBrightness=60; r.save(); r.run(10000);
        assert(!r.a.LightRebasePending() && r.brightness==60 && r.a.LightBaselineMedian()==130);
    }
    puts("saved default rebasing, stable/new outputs, manual/error/PIR deferral and sound preservation passed");
}
