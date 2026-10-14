#include "common/algorithm/algorithm.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <utility>
uint32_t testUptimeMs=0;

struct Rig {
    SensoryReliefController controller;
    AppConfig c;
    Rig() { c.reliefHoldMs=3000; } // Short fixture hold; production everyday default is tested separately.
    SensorSnapshot s{100,{.01f,.01f,0,0,true},true,0,true,true,true};
    SensoryTarget target{};
    uint32_t now=0;
    SensoryTarget tick(float score) {
        now+=250; s.sound.timestampMs=s.observedAtMs=now;
        return target=controller.Evaluate(s,c,score,now);
    }
    void relief() { tick(0); for(int i=0;i<40;++i) tick(4); assert(target.brightnessPercent==30 && target.fanPercent==20); }
};
int main() {
    for(int channel=0;channel<3;++channel) {
        auto& a=Algorithm::Instance(); AppConfig c; a.Initialize(c);
        SensorSnapshot s{100,{.01f,.01f,0,0,true},true,0,true,true,true};
        auto tick=[&](float lux,float rms) {
            testUptimeMs+=250; s.lux=lux; s.sound={rms,rms,0,testUptimeMs,true};
            auto score=a.EvaluateEnvironment(s); return std::make_pair(score,a.Evaluate(s,score));
        };
        for(int i=0;i<40;++i) tick(100,.01f);
        const float lux=channel==1?100:channel==2?103:120;
        const float rms=channel==0?.01f:channel==2?.014f:.1f;
        for(int i=0;i<3;++i) { auto t=tick(lux,rms).second; assert(t.brightnessPercent==60 && t.fanPercent==60); }
        auto result=tick(lux,rms);
        if(channel==2) assert(result.first.light<1.4f && result.first.sound<1.4f && result.first.combined>=1.4f);
        assert(result.second.brightnessPercent<60 && result.second.fanPercent<60 && result.second.cctMireds==c.preferredCCTMireds);
        assert(a.LightBaselineMedian()==100 && a.SoundBaselineMedian()==.01f);
    }
    {
        Rig r; r.relief();
        // Low input cannot begin recovery while either physical output is moving.
        r.s.controlInputsSettled=false;
        for(int i=0;i<80;++i) { auto t=r.tick(0); assert(t.brightnessPercent==30 && t.fanPercent==20); }
        r.s.controlInputsSettled=true;
        for(uint32_t i=0;i<=r.c.recoverConfirmMs/ALGORITHM_PERIOD_MS;++i) { auto t=r.tick(0); assert(t.brightnessPercent==30 && t.fanPercent==20); }
        r.tick(0); auto t=r.tick(0); assert(t.brightnessPercent==32 && t.fanPercent==22);
        // The middle band pauses both with one recovery clock.
        for(int i=0;i<40;++i) { auto h=r.tick(1); assert(h.brightnessPercent==t.brightnessPercent && h.fanPercent==t.fanPercent && h.cctMireds==t.cctMireds); }
        for(int i=0;i<9;++i) { auto h=r.tick(0); assert(h.brightnessPercent==t.brightnessPercent && h.fanPercent==t.fanPercent); }
        // A transient high event pauses recovery but cannot undo its output step.
        auto h=r.tick(2); assert(h.brightnessPercent==t.brightnessPercent && h.fanPercent==t.fanPercent);
        r.tick(1);
        for(int i=0;i<4;++i) r.tick(2);
        assert(r.target.brightnessPercent==30 && r.target.fanPercent==20);
        for(int i=0;i<100;++i) { auto held=r.tick(0); assert(held.brightnessPercent==30 && held.fanPercent==20); }
        // Improvement at the same output releases the single retry latch.
        r.s.lux=97;
        for(int i=0;i<120;++i) r.tick(0);
        assert(r.controller.IsNormal() && r.target.brightnessPercent==60 && r.target.fanPercent==60 && r.target.cctMireds==250);
    }
    {
        Rig r; r.relief(); for(int i=0;i<15;++i) r.tick(0);
        auto before=r.target; assert(before.fanPercent>20);
        r.s.illuminanceValid=false;
        for(int i=0;i<20;++i) assert(r.tick(0).brightnessPercent==before.brightnessPercent);
        r.s.illuminanceValid=true;
        for(int i=0;i<9;++i) assert(r.tick(0).brightnessPercent==before.brightnessPercent);
    }
    {
        Rig r; r.tick(0); r.tick(4);
        for(int i=0;i<3;++i) {r.now+=250; auto t=r.controller.Evaluate(r.s,r.c,4,r.now); assert(t.brightnessPercent==60 && t.fanPercent==60);}
        // A gap/invalid sample breaks confirmation; it does not inherit previous high time.
        r.now+=1250; r.s.sound.energy=std::numeric_limits<float>::quiet_NaN(); r.tick(4);
        r.s.sound.energy=r.s.sound.peak=.01f;
        for(int i=0;i<3;++i) assert(r.tick(4).brightnessPercent==60);
        assert(r.tick(4).brightnessPercent==30);
    }
    {
        Rig r; r.c.preferredFanPercent=0; r.c.minBrightness=r.c.preferredBrightness=0;
        for(int i=0;i<80;++i) {auto t=r.tick(4); assert(!t.on && t.fanPercent==0);}
        r.c.occupancyAutomationEnabled=true; r.s.occupied=false;
        assert(r.tick(4).fanPercent==0 && !r.target.on);
        r.s.occupied=true; r.c.preferredBrightness=60; r.c.minBrightness=30; r.c.preferredFanPercent=60;
        auto t=r.tick(0); assert(t.on && t.brightnessPercent==60 && t.fanPercent==60);
    }
    puts("joint light-only/sound-only/mixed entry, common hold/recovery/pause/rollback, invalid/stale input and PIR restart passed");
}
