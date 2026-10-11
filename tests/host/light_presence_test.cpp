#include "common/algorithm/light_presence.h"
#include "common/light_tone.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    AppConfig config;
    SensorSnapshot s = {100, {.01f, .01f, 0, 0, true}, true, 0, true, true, true};
    // Confirm the curve starts gently and increases monotonically to the lower bound.
    uint8_t previous = config.preferredBrightness;
    for (float score : {1.4f, 2.0f, 2.8f, 4.0f}) {
        LightPresenceController curve;
        LightTarget target{};
        for (uint32_t t=0;t<=750;t+=250) target=curve.Evaluate(s,config,score,t);
        assert(target.brightnessPercent <= previous && target.brightnessPercent >= config.minBrightness);
        if (score==1.4f) assert(target.brightnessPercent==98);
        if (score==4.0f) assert(target.brightnessPercent==config.minBrightness);
        previous=target.brightnessPercent;
    }
    for (uint16_t preference : {uint16_t(250), uint16_t(333), uint16_t(417)}) {
        LightPresenceController c;
        config.preferredCCTMireds = preference;
        assert(c.Evaluate(s, config, 0, 0).cctMireds == config.minCCTMireds);
        assert(c.Evaluate(s, config, 4, 250).brightnessPercent == 100);
        assert(c.Evaluate(s, config, 0, 500).brightnessPercent == 100); // Isolated spike discarded.
        for (uint32_t t = 1000; t <= 1750; t += 250) c.Evaluate(s, config, 4, t);
        auto held = c.Evaluate(s, config, 4, 2000);
        assert(held.brightnessPercent == config.minBrightness && held.cctMireds == preference);
        assert(c.Evaluate(s, config, 0, 2250).brightnessPercent == config.minBrightness);
        s.micValid = false;
        for (uint32_t t = 2500; t < 9000; t += 250) assert(c.Evaluate(s, config, 0, t).brightnessPercent == config.minBrightness);
        s.micValid = true;
        for (uint32_t t = 9000; t <= 40000; t += 250) {
            const auto output = c.Evaluate(s, config, 0, t);
            assert(output.brightnessPercent >= config.minBrightness && output.brightnessPercent <= 100);
        }
        assert(c.Evaluate(s, config, 0, 40500).brightnessPercent == 100);
        assert(c.Evaluate(s, config, 0, 40500).cctMireds == config.minCCTMireds);
    }
    LightPresenceController c;
    config.minBrightness = 45; config.maxBrightness = 90; config.preferredBrightness = 60;
    config.minCCTMireds = 300; config.maxCCTMireds = 400; config.preferredCCTMireds = 454;
    for (uint32_t t = 0; t <= 1000; t += 250) c.Evaluate(s, config, 4, t);
    assert(c.Evaluate(s, config, 4, 1250).brightnessPercent == 45);
    assert(c.Evaluate(s, config, 4, 1250).cctMireds == 400);
    s.occupied = false;
    assert(!c.Evaluate(s, config, 4, 1500).on);
    assert(c.Evaluate(s, config, 4, 1500).brightnessPercent == 0);
    s.occupied = true;
    assert(c.Evaluate(s, config, 0, 1750).brightnessPercent == 60);
    config.minBrightness = 0; config.preferredBrightness = 0;
    assert(!c.Evaluate(s, config, 0, 2000).on);
    assert(WhiteToneCoolPercent(333) == 60);
    config = {};
    c.Reset(); s.micValid = false;
    s.observedOutputs = {true, true, 25, 417}; s.observedAtMs = 3000;
    assert(c.Evaluate(s, config, 0, 3000).brightnessPercent == 25);
    s.observedOutputs.lightOn = false;
    assert(!c.Evaluate(s, config, 0, 3250).on);
    {
        LightPresenceController custom; AppConfig c;
        c.normalCCTMireds = 370; c.preferredCCTMireds = 250;
        c.minBrightness = 40; c.reliefHoldMs = 10000; c.recoverConfirmMs = 1000;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        assert(custom.Evaluate(s,c,0,0).cctMireds == 370);
        for (uint32_t t=250;t<=1000;t+=250) custom.Evaluate(s,c,4,t);
        assert(custom.Evaluate(s,c,4,1250).brightnessPercent == 40);
        assert(custom.Evaluate(s,c,4,1250).cctMireds == 250);
        for (uint32_t t=1500;t<=12000;t+=250) assert(custom.Evaluate(s,c,0,t).brightnessPercent == 40);
        for (uint32_t t=12250;t<=35000;t+=250) custom.Evaluate(s,c,0,t);
        auto restored = custom.Evaluate(s,c,0,35250);
        assert(restored.brightnessPercent == 100 && restored.cctMireds == 370);
    }
    {
        // A late spike must not borrow the time accumulated by weaker inputs.
        LightPresenceController c; AppConfig cfg;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        for (uint32_t t=0;t<750;t+=250) c.Evaluate(s,cfg,1.4f,t);
        auto target=c.Evaluate(s,cfg,4,750);
        assert(target.brightnessPercent==98 && target.cctMireds==cfg.preferredCCTMireds);
        // One stronger input during relief also leaves brightness unchanged.
        assert(c.Evaluate(s,cfg,4,1000).brightnessPercent==98);
        assert(c.Evaluate(s,cfg,1.4f,1250).brightnessPercent==98);
        for (uint32_t t=1500;t<2250;t+=250) assert(c.Evaluate(s,cfg,4,t).brightnessPercent==98);
        assert(c.Evaluate(s,cfg,4,2250).brightnessPercent==cfg.minBrightness);
        // The floor must not prevent a newly saved relief tone from applying.
        cfg.preferredCCTMireds=417;
        target=c.Evaluate(s,cfg,4,2500);
        assert(target.brightnessPercent==cfg.minBrightness && target.cctMireds==417);
        cfg.preferredCCTMireds=250;
        target=c.Evaluate(s,cfg,0,2750);
        assert(target.brightnessPercent==cfg.minBrightness && target.cctMireds==250);
    }
    {
        LightPresenceController c; AppConfig cfg;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        for (uint32_t t=0;t<=750;t+=250) c.Evaluate(s,cfg,1.4f,t);
        c.Evaluate(s,cfg,4,1000);
        s.controlInputsSettled=false; c.Evaluate(s,cfg,4,1250);
        s.controlInputsSettled=true;
        for (uint32_t t=1500;t<2250;t+=250) assert(c.Evaluate(s,cfg,4,t).brightnessPercent==98);
        assert(c.Evaluate(s,cfg,4,2250).brightnessPercent==cfg.minBrightness);
    }
    {
        LightPresenceController c; AppConfig cfg;
        s = {100, {.01f,.01f,0,0,true},true,0,true,true,true};
        for(uint32_t t=0;t<=750;t+=250) c.Evaluate(s,cfg,1.4f,t);
        for(uint32_t t=1000;t<1750;t+=250) c.Evaluate(s,cfg,2,t);
        auto target=c.Evaluate(s,cfg,4,1750);
        assert(target.brightnessPercent<98 && target.brightnessPercent>cfg.minBrightness);
        assert(c.Evaluate(s,cfg,2,2000).brightnessPercent==target.brightnessPercent);
    }
    puts("confirmed entry/escalation, live relief tone, recovery, bounds and absence passed");
}
