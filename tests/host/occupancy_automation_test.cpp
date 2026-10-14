#include "common/algorithm/algorithm.h"
#include <cassert>
#include <cstdio>
uint32_t testUptimeMs = 1000;
int main() {
    auto& a=Algorithm::Instance(); AppConfig c; c.occupancyTimeoutMs=30000; c.minBrightness=1; c.maxBrightness=100; c.preferredBrightness=100; c.reliefHoldMs=1500; c.occupancyAutomationEnabled=true;
    SensorSnapshot s={100,{.01f,.01f,0,1000,true},true,1000,true,true,true};
    auto at=[&](uint32_t now) {
        testUptimeMs=now; s.sound.timestampMs=now;
        return a.Evaluate(s,a.EvaluateEnvironment(s));
    };
    a.Initialize(c);
    auto t=at(1000); assert(t.lightOn && t.fanOn);
    s.occupied=false;
    t=at(30999); assert(t.lightOn && t.fanOn);
    t=at(31000); assert(!t.lightOn && !t.fanOn); // Full timeout, not one tick later.
    t=at(60000); assert(!t.lightOn && !t.fanOn);
    s.occupied=true; s.lastMotionMs=61000;
    t=at(61000); assert(t.lightOn && t.fanOn); // Movement restarts the automatic policy.
    s.occupied=false;
    t=at(90000); assert(t.lightOn && t.fanOn);
    s.occupied=true; s.lastMotionMs=90001; at(90001); s.occupied=false;
    t=at(119999); assert(t.lightOn && t.fanOn); // Repeated motion renews the hold.
    t=at(120001); assert(!t.lightOn && !t.fanOn);
    c.occupancyAutomationEnabled=false; a.SetConfig(c);
    t=at(120250); assert(t.lightOn && t.fanOn); // Disabling resumes an absence-stopped AUTO.
    s.pirValid=false;
    for(int i=0;i<500;++i) { t=at(120500+250*i); assert(t.lightOn && t.fanOn); }
    assert(!s.occupied && !s.pirValid); // Actual sensing/telemetry was never overwritten.
    c.occupancyAutomationEnabled=true; a.SetConfig(c); s.pirValid=true;
    t=at(300000); assert(!t.lightOn && !t.fanOn);
    s.occupied=true; s.lastMotionMs=300250;
    t=at(300250); assert(t.lightOn && t.fanOn);
    // A settings limit of zero still keeps the relevant output off when motion returns.
    c.preferredFanPercent=0; a.SetConfig(c);
    t=at(300500); assert(t.lightOn && !t.fanOn);
    // Uptime wrap does not end an active motion hold early.
    c.preferredFanPercent=60; a.Initialize(c);
    s.lastMotionMs=0xfffffff0U; s.occupied=true; at(s.lastMotionMs);
    s.occupied=false; t=at(1000); assert(t.lightOn && t.fanOn);
    t=at(30000); assert(!t.lightOn && !t.fanOn);
    puts("occupancy enable/disable, hold boundary/renewal, motion restart, telemetry and wrap passed");
}
