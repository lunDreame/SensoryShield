#include "common/algorithm/sensory_relief.h"
#include "common/light_tone.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

SensorSnapshot Fresh(SensorSnapshot s, uint32_t now) { s.sound.timestampMs=now; s.observedAtMs=now; return s; }
int main() {
    AppConfig config; config.preferredFanPercent=0; config.minBrightness=1; config.maxBrightness=100; config.preferredBrightness=100; config.reliefHoldMs=1500; config.occupancyAutomationEnabled=true;
    SensorSnapshot s = {100, {.01f, .01f, 0, 0, true}, true, 0, true, true, true};
    // Confirm the curve starts gently and increases monotonically to the lower bound.
    uint8_t previous = config.preferredBrightness;
    for (float score : {1.4f, 2.0f, 2.8f, 4.0f}) {
        SensoryReliefController curve;
        SensoryTarget target{};
        for (uint32_t t=0;t<=750;t+=250) target=curve.Evaluate(Fresh(s,t),config,score,t);
        assert(target.brightnessPercent <= previous && target.brightnessPercent >= config.minBrightness);
        if (score==1.4f) assert(target.brightnessPercent==98);
        if (score==4.0f) assert(target.brightnessPercent==config.minBrightness);
        previous=target.brightnessPercent;
    }
    for (uint16_t preference : {uint16_t(250), uint16_t(333), uint16_t(417)}) {
        SensoryReliefController c;
        config.preferredCCTMireds = preference;
        assert(c.Evaluate(Fresh(s,0),config,0,0).cctMireds == config.minCCTMireds);
        assert(c.Evaluate(Fresh(s,250),config,4,250).brightnessPercent == 100);
        assert(c.Evaluate(Fresh(s,500),config,0,500).brightnessPercent == 100); // Isolated spike discarded.
        for (uint32_t t = 1000; t <= 1750; t += 250) c.Evaluate(Fresh(s,t),config,4,t);
        auto held = c.Evaluate(Fresh(s,2000),config,4,2000);
        assert(held.brightnessPercent == config.minBrightness && held.cctMireds == preference);
        assert(c.Evaluate(Fresh(s,2250),config,0,2250).brightnessPercent == config.minBrightness);
        s.micValid = false;
        for (uint32_t t = 2500; t < 9000; t += 250) assert(c.Evaluate(Fresh(s,t),config,0,t).brightnessPercent == config.minBrightness);
        s.micValid = true;
        for (uint32_t t = 9000; t <= 40000; t += 250) {
            const auto output = c.Evaluate(Fresh(s,t),config,0,t);
            assert(output.brightnessPercent >= config.minBrightness && output.brightnessPercent <= 100);
        }
        assert(c.Evaluate(Fresh(s,40500),config,0,40500).brightnessPercent == 100);
        assert(c.Evaluate(Fresh(s,40500),config,0,40500).cctMireds == config.minCCTMireds);
    }
    SensoryReliefController c;
    config.minBrightness = 45; config.maxBrightness = 90; config.preferredBrightness = 60;
    config.minCCTMireds = 300; config.maxCCTMireds = 400; config.preferredCCTMireds = 454;
    for (uint32_t t = 0; t <= 1000; t += 250) c.Evaluate(Fresh(s,t),config,4,t);
    assert(c.Evaluate(Fresh(s,1250),config,4,1250).brightnessPercent == 45);
    assert(c.Evaluate(Fresh(s,1250),config,4,1250).cctMireds == 400);
    s.occupied = false;
    assert(!c.Evaluate(Fresh(s,1500),config,4,1500).on);
    assert(c.Evaluate(Fresh(s,1500),config,4,1500).brightnessPercent == 0);
    s.occupied = true;
    assert(c.Evaluate(Fresh(s,1750),config,0,1750).brightnessPercent == 60);
    config.minBrightness = 0; config.preferredBrightness = 0;
    assert(!c.Evaluate(Fresh(s,2000),config,0,2000).on);
    assert(WhiteToneCoolPercent(333) == 60);
    config = {};
    c.Reset(); s.micValid = false;
    s.observedOutputs = {true, true, 25, 417}; s.observedAtMs = 3000;
    assert(c.Evaluate(Fresh(s,3000),config,0,3000).brightnessPercent == 25);
    s.observedOutputs.lightOn = false;
    assert(!c.Evaluate(Fresh(s,3250),config,0,3250).on);
    {
        SensoryReliefController custom; AppConfig c; c.preferredFanPercent=0; c.minBrightness=1; c.maxBrightness=100; c.preferredBrightness=100; c.reliefHoldMs=1500; c.occupancyAutomationEnabled=true;
        c.normalCCTMireds = 370; c.preferredCCTMireds = 250;
        c.minBrightness = 40; c.reliefHoldMs = 10000; c.recoverConfirmMs = 1000;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        assert(custom.Evaluate(Fresh(s,0),c,0,0).cctMireds == 370);
        for (uint32_t t=250;t<=1000;t+=250) custom.Evaluate(Fresh(s,t),c,4,t);
        assert(custom.Evaluate(Fresh(s,1250),c,4,1250).brightnessPercent == 40);
        assert(custom.Evaluate(Fresh(s,1250),c,4,1250).cctMireds == 250);
        for (uint32_t t=1500;t<=12000;t+=250) assert(custom.Evaluate(Fresh(s,t),c,0,t).brightnessPercent == 40);
        for (uint32_t t=12250;t<=35000;t+=250) custom.Evaluate(Fresh(s,t),c,0,t);
        auto restored = custom.Evaluate(Fresh(s,35250),c,0,35250);
        assert(restored.brightnessPercent == 100 && restored.cctMireds == 370);
    }
    {
        // A late spike must not borrow the time accumulated by weaker inputs.
        SensoryReliefController c; AppConfig cfg; cfg.preferredFanPercent=0; cfg.minBrightness=1; cfg.maxBrightness=100; cfg.preferredBrightness=100; cfg.reliefHoldMs=1500; cfg.occupancyAutomationEnabled=true;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        for (uint32_t t=0;t<750;t+=250) c.Evaluate(Fresh(s,t),cfg,1.4f,t);
        auto target=c.Evaluate(Fresh(s,750),cfg,4,750);
        assert(target.brightnessPercent==98 && target.cctMireds==cfg.preferredCCTMireds);
        // One stronger input during relief also leaves brightness unchanged.
        assert(c.Evaluate(Fresh(s,1000),cfg,4,1000).brightnessPercent==98);
        assert(c.Evaluate(Fresh(s,1250),cfg,1.4f,1250).brightnessPercent==98);
        for (uint32_t t=1500;t<2250;t+=250) assert(c.Evaluate(Fresh(s,t),cfg,4,t).brightnessPercent==98);
        assert(c.Evaluate(Fresh(s,2250),cfg,4,2250).brightnessPercent==cfg.minBrightness);
        // The floor must not prevent a newly saved relief tone from applying.
        cfg.preferredCCTMireds=417;
        target=c.Evaluate(Fresh(s,2500),cfg,4,2500);
        assert(target.brightnessPercent==cfg.minBrightness && target.cctMireds==417);
        cfg.preferredCCTMireds=250;
        target=c.Evaluate(Fresh(s,2750),cfg,0,2750);
        assert(target.brightnessPercent==cfg.minBrightness && target.cctMireds==250);
    }
    {
        SensoryReliefController c; AppConfig cfg; cfg.preferredFanPercent=0; cfg.minBrightness=1; cfg.maxBrightness=100; cfg.preferredBrightness=100; cfg.reliefHoldMs=1500; cfg.occupancyAutomationEnabled=true;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        for (uint32_t t=0;t<=750;t+=250) c.Evaluate(Fresh(s,t),cfg,1.4f,t);
        c.Evaluate(Fresh(s,1000),cfg,4,1000);
        s.controlInputsSettled=false; c.Evaluate(Fresh(s,1250),cfg,4,1250);
        s.controlInputsSettled=true;
        for (uint32_t t=1500;t<2250;t+=250) assert(c.Evaluate(Fresh(s,t),cfg,4,t).brightnessPercent==98);
        assert(c.Evaluate(Fresh(s,2250),cfg,4,2250).brightnessPercent==cfg.minBrightness);
    }
    {
        SensoryReliefController c; AppConfig cfg; cfg.preferredFanPercent=0; cfg.minBrightness=1; cfg.maxBrightness=100; cfg.preferredBrightness=100; cfg.reliefHoldMs=1500; cfg.occupancyAutomationEnabled=true;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        for(uint32_t t=0;t<=750;t+=250) c.Evaluate(Fresh(s,t),cfg,1.4f,t);
        for(uint32_t t=1000;t<1750;t+=250) c.Evaluate(Fresh(s,t),cfg,2,t);
        auto target=c.Evaluate(Fresh(s,1750),cfg,4,1750);
        assert(target.brightnessPercent<98 && target.brightnessPercent>cfg.minBrightness);
        assert(c.Evaluate(Fresh(s,2000),cfg,2,2000).brightnessPercent==target.brightnessPercent);
    }
    puts("confirmed entry/escalation, live relief tone, recovery, bounds and absence passed");
}
