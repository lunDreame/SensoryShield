#include "common/algorithm/light_presence.h"
#include "common/light_tone.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    AppConfig config;
    SensorSnapshot s = {100, {.01f, .01f, 0, 0, true}, true, 0, true, true, true};
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
    puts("confirmed relief, preferred relief tone, stable recovery, bounds and unchanged absence passed");
}
