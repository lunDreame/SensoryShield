#include "settings_fixture.h"
#include "common/algorithm/algorithm.h"
uint32_t testUptimeMs=1000;
int main() {
    const AppConfig c;
#if SENSORYSHIELD_DEMO_DEFAULTS
    assert(c.reliefFanPercent == 0);
    assert(c.reliefHoldMs==5000 && c.recoverConfirmMs==3000 && c.occupancyTimeoutMs==30000);
    assert(c.preferredBrightness==100 && c.preferredFanPercent==100 && c.maxBrightness==100);
    assert(CHILD_HOLD_MS==5000);
#else
    assert(c.reliefFanPercent == 20);
    assert(c.reliefHoldMs==1800000 && c.recoverConfirmMs==3000 && c.occupancyTimeoutMs==900000);
    assert(c.preferredBrightness==60 && c.preferredFanPercent==60);
    assert(CHILD_HOLD_MS==1800000);
#endif
    assert(c.preferredCCTMireds==454);
    assert(!c.occupancyAutomationEnabled); // Both profiles let guardians opt into absence shutdown.
    auto& m=Memory::Instance(); assert(m.Initialize()==0);
    AppConfig saved=c; saved.reliefHoldMs=17000; saved.recoverConfirmMs=2500; saved.occupancyTimeoutMs=900000;
    assert(m.SaveConfig(saved)==0 && m.Initialize()==0);
    assert(m.Config().reliefHoldMs==17000 && m.Config().recoverConfirmMs==2500 && m.Config().occupancyTimeoutMs==900000);
    // Upgrade the previously deployed v9 profile without losing other choices.
    saved.reliefFanPercent=20; saved.lightWeight=.3f; saved.preferredCCTMireds=333;
    assert(m.SaveConfig(saved)==0);
    uint32_t oldVersion=OCCUPANCY_CONFIG_VERSION;
    memcpy(storage["sensoryshield/app_config"].data(),&oldVersion,sizeof(oldVersion));
    assert(m.Initialize()==0);
    assert(m.Config().reliefFanPercent==AUTO_FAN_QUIET_PERCENT);
    assert(!m.Config().occupancyAutomationEnabled && m.Config().lightWeight==.3f);
    assert(m.Config().preferredCCTMireds==333 && m.Config().reliefHoldMs==17000);
    saved.reliefFanPercent=40; assert(m.SaveConfig(saved)==0);
    memcpy(storage["sensoryshield/app_config"].data(),&oldVersion,sizeof(oldVersion));
    assert(m.Initialize()==0 && m.Config().reliefFanPercent==40);
    // New explicit settings are never remigrated on a later boot.
    saved.reliefFanPercent=20; assert(m.SaveConfig(saved)==0);
    assert(m.Initialize()==0 && m.Config().reliefFanPercent==20);
    auto& a=Algorithm::Instance();a.Initialize(c);
    InitialEnvironment seed{};seed.count=31;
    for(int i=0;i<31;++i){seed.lux[i]=150;seed.sound[i]=.015f;}
    assert(a.InstallRehearsalBaseline(seed)==bool(SENSORYSHIELD_DEMO_DEFAULTS));
    assert(a.BaselineFrozen()==bool(SENSORYSHIELD_DEMO_DEFAULTS));
#if !SENSORYSHIELD_DEMO_DEFAULTS
    SensorSnapshot sample{100,{.01f,.01f,0,0,true},true,0,true,true,true};
    for(int i=0;i<40;++i){testUptimeMs+=250;sample.sound.timestampMs=testUptimeMs;a.EvaluateEnvironment(sample);}
    assert(a.LightBaselineMedian()==100 && !a.BaselineFrozen());
#endif
    puts(SENSORYSHIELD_DEMO_DEFAULTS?"demo initial defaults and saved-setting precedence passed":"everyday initial examples and saved-setting precedence passed");
}
