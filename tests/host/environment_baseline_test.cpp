#include "common/algorithm/algorithm.h"
#include "system/memory.h"
#include "zephyr/settings/settings.h"
#include <cassert>
#include <cstring>
#include <cerrno>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
uint32_t testUptimeMs = 1000;
static std::map<std::string,std::vector<unsigned char>> flash;
static bool failWrite = false;
int settings_subsys_init() { return 0; }
int settings_save_one(const char* key, const void* data, size_t size) {
    if (failWrite) return -EIO;
    const auto* bytes=static_cast<const unsigned char*>(data);
    flash[key]=std::vector<unsigned char>(bytes,bytes+size);return 0;
}
int settings_delete(const char* key) {flash.erase(key);return 0;}
int settings_load_subtree_direct(const char* key, settings_load_direct_cb callback, void* param) {
    const auto it=flash.find(key); if(it==flash.end()) return 0;
    auto bytes=it->second;
    return callback("",bytes.size(),[](void* arg,void* out,size_t size){
        auto* bytes=static_cast<std::vector<unsigned char>*>(arg);
        std::memcpy(out,bytes->data(),size);return static_cast<int>(size);
    },&bytes,param);
}
int main() {
    auto& memory=Memory::Instance();
    EnvironmentBaseline restored;
    assert(memory.LoadEnvironmentBaseline(&restored)==-ENOENT);
    EnvironmentBaseline measured{1,100,2,0.01f,0.005f,20};
    assert(memory.SaveEnvironmentBaseline(measured)==0);
    assert(memory.LoadEnvironmentBaseline(&restored)==0);
    assert(restored.luxMedian==100 && restored.samples==20);
    auto invalid=measured;invalid.soundMad=std::numeric_limits<float>::quiet_NaN();
    assert(memory.SaveEnvironmentBaseline(invalid)==-EINVAL);
    invalid=measured;invalid.samples=19;
    assert(memory.SaveEnvironmentBaseline(invalid)==-EINVAL);
    failWrite=true;auto replacement=measured;replacement.luxMedian=200;
    assert(memory.SaveEnvironmentBaseline(replacement)==-EIO);failWrite=false;
    assert(memory.LoadEnvironmentBaseline(&restored)==0 && restored.luxMedian==100);
    auto& algorithm=Algorithm::Instance(); AppConfig config;
    algorithm.Initialize(config);algorithm.SetEnvironmentBaseline(restored);
    SensorSnapshot snapshot{120,{0.01f,0.01f,0,testUptimeMs,true},true,testUptimeMs,true,true,true};
    assert(algorithm.EvaluateEnvironment(snapshot).combined==STIMULUS_MAX_SCORE);
    // A fixed reference must not adapt to a darker interval.
    snapshot.lux=20;
    for(int i=0;i<50;i++) assert(algorithm.EvaluateEnvironment(snapshot).combined==0);
    snapshot.lux=100;assert(algorithm.EvaluateEnvironment(snapshot).combined==0);
    // Simulate restart: restored baseline applies immediately, without warm-up.
    algorithm.Initialize(config);algorithm.SetEnvironmentBaseline(restored);
    snapshot.lux=120;assert(algorithm.EvaluateEnvironment(snapshot).combined==STIMULUS_MAX_SCORE);
    snapshot.lux=100;SoundFanController fan;fan.SetEnvironmentBaseline(restored);
    uint8_t speed=0;
    for(int i=0;i<30;i++){testUptimeMs+=250;snapshot.sound.timestampMs=testUptimeMs;speed=fan.Evaluate(snapshot,config,testUptimeMs);}
    assert(speed==AUTO_FAN_NORMAL_PERCENT);
    snapshot.sound.energy=0.10f;snapshot.sound.peak=0.10f;
    for(int i=0;i<40;i++){testUptimeMs+=250;snapshot.sound.timestampMs=testUptimeMs;speed=fan.Evaluate(snapshot,config,testUptimeMs);}
    assert(speed==AUTO_FAN_QUIET_PERCENT);
    // Replacement resets both controllers to the new reference.
    algorithm.SetEnvironmentBaseline(replacement);snapshot.lux=200;snapshot.sound.energy=.01f;
    assert(algorithm.EvaluateEnvironment(snapshot).combined==0);
    flash["sensoryshield/environment_baseline"]=std::vector<unsigned char>(1);
    assert(memory.LoadEnvironmentBaseline(&restored)==-EINVAL);
    assert(!IsValidEnvironmentBaseline(restored));
    invalid=measured;invalid.version=2;
    settings_save_one("sensoryshield/environment_baseline",&invalid,sizeof(invalid));
    assert(memory.LoadEnvironmentBaseline(&restored)==-EINVAL);
    assert(!IsValidEnvironmentBaseline(restored));
    memory.SaveEnvironmentBaseline(measured);memory.FactoryReset();
    assert(memory.LoadEnvironmentBaseline(&restored)==-ENOENT);
    puts("baseline storage, restore, corruption, write failure, fixed scoring and fan response passed");
}
