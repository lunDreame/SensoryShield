#include "common/algorithm/sound_fan.h"
#include <cassert>
#include <cstdio>
#include <limits>

struct Rig {
    SoundFanController controller;
    AppConfig config;
    SensorSnapshot snapshot = {100, {0.01f, 0.02f, 0, 0, true}, true, 0, true, true, true};
    uint32_t now = 0;
    uint8_t tick(float energy = 0.01f) {
        now += 250;
        snapshot.sound = {energy, energy * 1.5f, 0, now, true};
        return controller.Evaluate(snapshot, config, now);
    }
    void warm() { for (int i=0; i<20; ++i) tick(); }
};
int main() {
    {
        Rig r; r.warm(); assert(r.tick() == 30);
        assert(r.tick(0.15f) == 30); // Isolated loud window.
        assert(r.tick() == 30);
        for (int i=0; i<60; ++i) r.tick(0.15f);
        assert(r.tick(0.15f) == 18); // Sustained sound cannot disappear into baseline.
        for (int i=0; i<10; ++i) assert(r.tick() == 18);
        for (int i=0; i<20; ++i) r.tick();
        assert(r.tick() == 30);
    }
    {
        Rig r; r.warm();
        for(int i=0;i<80;++i) assert(r.tick(i%2 ? 0.01f : 0.15f) == 30);
        r.snapshot.occupied = false; assert(r.tick() == 0);
        r.snapshot.occupied = true; assert(r.tick() == 18);
        r.snapshot.pirValid = false; assert(r.tick() == 0);
    }
    {
        Rig r; r.warm(); r.config.fanMaxPercent=20; assert(r.tick()==20);
        r.config.fanMaxPercent=10; assert(r.tick()==0);
        r.config.fanMaxPercent=0; assert(r.tick()==0);
        r.config.fanMaxPercent=80; r.warm();
        r.snapshot.micValid=false; assert(r.tick()==18);
        r.snapshot.micValid=true; r.warm();
        r.now+=2000; assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==18);
        r.snapshot.sound.energy=std::numeric_limits<float>::quiet_NaN();
        assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==18);
    }
    {
        Rig r; r.warm();
        r.now+=250; r.snapshot.sound.timestampMs=r.now;
        r.snapshot.sound.energy=-1; assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==18);
        r.snapshot.sound={0.1f,0.05f,0,r.now,true};
        assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==18);
    }
    {
        Rig r; r.warm(); r.config.soundWeight=0;
        for(int i=0;i<30;++i) assert(r.tick(0.2f)==30);
        const auto sample=r.snapshot; const auto before=r.controller.Evaluate(sample,r.config,r.now);
        r.now+=250; assert(r.controller.Evaluate(sample,r.config,r.now)==before);
        // Duplicate samples must not confirm persistent noise.
        r.config.soundWeight=1;
        r.tick(0.2f);
        for(int i=0;i<4;++i) {r.now+=250; assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==30);}
    }
    {
        Rig r; r.now=UINT32_MAX-3000; r.warm(); assert(r.tick()==30);
        for(int i=0;i<40;++i) r.tick(0.15f);
        assert(r.tick(0.15f)==18); // Unsigned time wrap.
    }
    puts("sound/fan scenarios passed");
}
