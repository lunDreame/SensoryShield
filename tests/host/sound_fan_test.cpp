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
    void warm() { for (int i=0; i<80; ++i) tick(); }
};
int main() {
    {
        Rig r; r.warm(); assert(r.tick() == AUTO_FAN_NORMAL_PERCENT);
        assert(r.tick(0.15f) == AUTO_FAN_NORMAL_PERCENT); // Isolated loud window.
        assert(r.tick() == AUTO_FAN_NORMAL_PERCENT);
        for (int i=0; i<60; ++i) r.tick(0.15f);
        assert(r.tick(0.15f) == AUTO_FAN_QUIET_PERCENT); // Sustained sound cannot disappear into baseline.
        for (int i=0; i<10; ++i) assert(r.tick() == AUTO_FAN_QUIET_PERCENT);
        for (int i=0; i<40; ++i) r.tick();
        assert(r.tick() == AUTO_FAN_NORMAL_PERCENT);
    }
    {
        Rig r; r.warm();
        for(int i=0;i<80;++i) assert(r.tick(i%2 ? 0.01f : 0.15f) == AUTO_FAN_NORMAL_PERCENT);
        r.snapshot.occupied = false; assert(r.tick() == 0);
        r.snapshot.occupied = true; assert(r.tick() == AUTO_FAN_QUIET_PERCENT);
        r.snapshot.pirValid = false; assert(r.tick() == 0);
    }
    {
        Rig r; r.warm(); r.config.fanMaxPercent=20; assert(r.tick()==20);
        r.config.fanMaxPercent=10; assert(r.tick()==10);
        r.config.fanMaxPercent=0; assert(r.tick()==0);
        r.config.fanMaxPercent=80; r.warm();
        r.snapshot.micValid=false; assert(r.tick()==AUTO_FAN_QUIET_PERCENT);
        r.snapshot.micValid=true; r.warm();
        r.now+=2000; assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==AUTO_FAN_QUIET_PERCENT);
        r.snapshot.sound.energy=std::numeric_limits<float>::quiet_NaN();
        assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==AUTO_FAN_QUIET_PERCENT);
    }
    {
        Rig r; r.warm();
        r.now+=250; r.snapshot.sound.timestampMs=r.now;
        r.snapshot.sound.energy=-1; assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==AUTO_FAN_QUIET_PERCENT);
        r.snapshot.sound={0.1f,0.05f,0,r.now,true};
        assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==AUTO_FAN_QUIET_PERCENT);
    }
    {
        Rig r; r.warm(); r.config.soundWeight=0;
        for(int i=0;i<30;++i) assert(r.tick(0.2f)==AUTO_FAN_NORMAL_PERCENT);
        const auto sample=r.snapshot; const auto before=r.controller.Evaluate(sample,r.config,r.now);
        r.now+=250; assert(r.controller.Evaluate(sample,r.config,r.now)==before);
        // Duplicate samples must not confirm persistent noise.
        r.config.soundWeight=1;
        r.tick(0.2f);
        for(int i=0;i<4;++i) {r.now+=250; assert(r.controller.Evaluate(r.snapshot,r.config,r.now)==AUTO_FAN_NORMAL_PERCENT);}
    }
    {
        Rig r; r.now=UINT32_MAX-3000; r.warm(); assert(r.tick()==AUTO_FAN_NORMAL_PERCENT);
        for(int i=0;i<40;++i) r.tick(0.15f);
        assert(r.tick(0.15f)==AUTO_FAN_QUIET_PERCENT); // Unsigned time wrap.
    }
    {
        Rig r; r.config.preferredFanPercent=40; r.warm(); assert(r.tick()==40);
        for(int i=0;i<40;++i) r.tick(.15f);
        assert(r.tick(.15f)==20);
        for(int i=0;i<40;++i) r.tick();
        assert(r.tick()==40); // Recovery returns to the chosen speed, not the old constant 60.
        r.config.preferredFanPercent=10; r.warm(); assert(r.tick()==10);
        for(int i=0;i<40;++i) assert(r.tick(.15f)<=10); // Quiet output cannot exceed the preference.
        r.snapshot.micValid=false; assert(r.tick()<=10);
        r.config.preferredFanPercent=0; assert(r.tick()==0);
        r.snapshot.micValid=true; for(int i=0;i<40;++i) assert(r.tick()==0);
    }
    puts("sound/fan scenarios and preferred speed recovery passed");
}
