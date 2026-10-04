#include "common/algorithm/light_presence.h"

#include <cassert>
#include <cstdio>

int main() {
    LightPresenceController controller;
    AppConfig config;
    SensorSnapshot snapshot = {100.0f, {0.0f, 0.0f, 0.0f, 0, true}, true, 0, true, true, true};

    {
        const LightTarget target = controller.Evaluate(snapshot, config, 0.0f);
        assert(target.on);
        assert(target.brightnessPercent == config.maxBrightness);
        assert(target.cctMireds == config.minCCTMireds);
    }
    {
        const LightTarget target = controller.Evaluate(snapshot, config, 4.0f);
        assert(target.on);
        assert(target.brightnessPercent == 45);
        assert(target.cctMireds == 352);
    }
    {
        const LightTarget target = controller.Evaluate(snapshot, config, 8.0f);
        assert(target.on);
        assert(target.brightnessPercent == config.minBrightness);
        assert(target.cctMireds == config.maxCCTMireds);
    }
    {
        const LightTarget below = controller.Evaluate(snapshot, config, -2.0f);
        const LightTarget above = controller.Evaluate(snapshot, config, 20.0f);
        assert(below.brightnessPercent == config.maxBrightness);
        assert(below.cctMireds == config.minCCTMireds);
        assert(above.brightnessPercent == config.minBrightness);
        assert(above.cctMireds == config.maxCCTMireds);
    }
    {
        snapshot.occupied = false;
        const LightTarget target = controller.Evaluate(snapshot, config, 4.0f);
        assert(!target.on);
        assert(target.brightnessPercent == 0);
        assert(target.cctMireds == config.maxCCTMireds);
    }

    puts("light/presence behavior preserved");
}
