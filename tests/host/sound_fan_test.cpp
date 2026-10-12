#include "fan_probe.h"
#include <cassert>
#include <cstdio>
#include <limits>

struct Rig {
    FanProbe controller;
    StimulusScorer scorer;
    AppConfig config;
    Rig() { config.occupancyAutomationEnabled=true; config.reliefHoldMs=1500; }
    SensorSnapshot snapshot = {100, {0.01f, 0.02f, 0, 0, true}, true, 0, true, true, true};
    uint32_t now = 0;
    uint8_t evaluate() {
        auto learning = snapshot;
        learning.learnLight = learning.learnSound = controller.IsNormal();
        return controller.Evaluate(snapshot, config, scorer.Evaluate(learning, config), now);
    }
    uint8_t tick(float energy = 0.01f) {
        now += 250;
        snapshot.sound = {energy, energy * 1.5f, 0, now, true};
        return evaluate();
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
        r.snapshot.occupied = true; assert(r.tick() == AUTO_FAN_NORMAL_PERCENT);
        r.snapshot.pirValid = false; assert(r.tick() == 0);
    }
    {
        Rig r; r.warm(); r.config.fanMaxPercent=20; assert(r.tick()==20);
        r.config.fanMaxPercent=18; assert(r.tick()==18);
        r.config.fanMaxPercent=10; assert(r.tick()==0);
        r.config.fanMaxPercent=0; assert(r.tick()==0);
        r.config.fanMaxPercent=80; r.warm();
        r.snapshot.micValid=false; assert(r.tick()==AUTO_FAN_QUIET_PERCENT);
        r.snapshot.micValid=true; r.warm();
        r.now+=2000; assert(r.evaluate()==AUTO_FAN_QUIET_PERCENT);
        r.snapshot.sound.energy=std::numeric_limits<float>::quiet_NaN();
        assert(r.evaluate()==AUTO_FAN_QUIET_PERCENT);
    }
    {
        Rig r; r.warm(); r.snapshot.controlInputsSettled=false;
        for(int i=0;i<20;++i) assert(r.tick(.15f)==AUTO_FAN_NORMAL_PERCENT);
        r.snapshot.controlInputsSettled=true;
        for(int i=0;i<3;++i) assert(r.tick(.15f)==AUTO_FAN_NORMAL_PERCENT);
        assert(r.tick(.15f)<AUTO_FAN_NORMAL_PERCENT); // Boost/error/settling time cannot confirm entry.
    }
    {
        Rig r; r.warm();
        r.now+=250; r.snapshot.sound.timestampMs=r.now;
        r.snapshot.sound.energy=-1; assert(r.evaluate()==AUTO_FAN_QUIET_PERCENT);
        r.snapshot.sound={0.1f,0.05f,0,r.now,true};
        assert(r.evaluate()==AUTO_FAN_QUIET_PERCENT);
    }
    {
        Rig r; r.warm(); r.config.soundWeight=0;
        for(int i=0;i<30;++i) assert(r.tick(0.2f)==AUTO_FAN_NORMAL_PERCENT);
        const auto before=r.evaluate();
        r.now+=250; assert(r.evaluate()==before);
        // Duplicate samples must not confirm persistent noise.
        r.config.soundWeight=1;
        r.tick(0.2f);
        for(int i=0;i<4;++i) {r.now+=250; assert(r.evaluate()==AUTO_FAN_NORMAL_PERCENT);}
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
        r.config.preferredFanPercent=10; r.warm(); assert(r.tick()==18);
        for(int i=0;i<40;++i) assert(r.tick(.15f)<=18); // Minimum duty is normalized once the cap permits it.
        r.snapshot.micValid=false; assert(r.tick()<=18);
        r.config.preferredFanPercent=0; assert(r.tick()==0);
        r.snapshot.micValid=true; for(int i=0;i<40;++i) assert(r.tick()==0);
    }
    {
        Rig r; r.config.reliefFanPercent = 0; r.config.reliefHoldMs = 20000; r.config.recoverConfirmMs = 1000;
        r.warm(); assert(r.tick() == 60); // Warm-up at zero must still permit normal operation.
        for (int i=0;i<40;++i) r.tick(.15f);
        assert(r.tick(.15f) == 0);
        for (int i=0;i<8;++i) assert(r.tick() == 0); // Minimum hold blocks premature recovery.
        for (int i=0;i<140;++i) r.tick();
        assert(r.tick() == 60);
        r.config.reliefFanPercent = 40;
        for (int i=0;i<40;++i) r.tick(.15f);
        assert(r.tick(.15f) == 40);
        r.config.preferredFanPercent = 30;
        assert(r.tick(.15f) <= 30); // Relief cannot increase an already lower normal output.
    }
    puts("sound/fan scenarios and preferred speed recovery passed");
}
