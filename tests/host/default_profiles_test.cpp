#include "settings_fixture.h"
int main() {
    const AppConfig c;
#if SENSORYSHIELD_DEMO_DEFAULTS
    assert(c.reliefHoldMs==3000 && c.recoverConfirmMs==3000 && c.occupancyTimeoutMs==30000);
#else
    assert(c.reliefHoldMs==1800000 && c.recoverConfirmMs==3000 && c.occupancyTimeoutMs==900000);
#endif
    assert(c.preferredBrightness==60 && c.preferredFanPercent==60);
    assert(!c.occupancyAutomationEnabled); // Both profiles let guardians opt into absence shutdown.
    auto& m=Memory::Instance(); assert(m.Initialize()==0);
    AppConfig saved=c; saved.reliefHoldMs=17000; saved.recoverConfirmMs=2500; saved.occupancyTimeoutMs=900000;
    assert(m.SaveConfig(saved)==0 && m.Initialize()==0);
    assert(m.Config().reliefHoldMs==17000 && m.Config().recoverConfirmMs==2500 && m.Config().occupancyTimeoutMs==900000);
    puts(SENSORYSHIELD_DEMO_DEFAULTS?"demo initial defaults and saved-setting precedence passed":"everyday initial examples and saved-setting precedence passed");
}
