#include <zephyr/kernel.h>
#include <future>
#include <condition_variable>
#include <atomic>
#include <cassert>
#include <cstdio>
#include "settings_fixture.h"
#define private public
#include "system/system.h"
#undef private
#include "common/algorithm/algorithm.h"
#include "common/device/Light.h"
#include "common/device/Fan.h"
#include "peripheral/Fan.h"
#include "peripheral/WS2812B.h"
#include "peripheral/BH1750.h"
#include "peripheral/Mic.h"
#include "system/matter_bridge.h"
#include "system/threadrest.h"

uint32_t testUptimeMs=1000;
int hostPwmCallCount=0,hostPwmFailCall=-1; uint8_t hostPwmDuties[64]={};
std::mutex ledLock; std::condition_variable ledSignal;
bool ledBlocked=false,ledReleased=false;
std::atomic<bool> pauseNextLed{false};

// Keep the production System/Algorithm/devices/Fan, replacing only unavailable
// light/sensor hardware and the Matter transport. No controller logic is mocked.
WS2812B& WS2812B::Instance() {static WS2812B w;return w;}
int WS2812B::Initialize() {return 0;}
int WS2812B::SetWhiteTarget(bool on,uint8_t brightness,uint8_t cool) {
    if(pauseNextLed.exchange(false)) {
        std::unique_lock<std::mutex> lock(ledLock); ledBlocked=true; ledSignal.notify_all();
        ledSignal.wait(lock,[]{return ledReleased;});
    }
    mState={on,brightness,cool,false,255,255,255}; return 0;
}
int WS2812B::SetRgbTarget(bool on,uint8_t b,uint8_t r,uint8_t g,uint8_t blue) {mState={on,b,0,true,r,g,blue};return 0;}
BH1750& BH1750::Instance() {static BH1750 b;return b;}
int BH1750::Initialize() {mHealthy=true;mErrorCount=0;return 0;}
int BH1750::ReadLux(float* lux) {++mErrorCount;*lux=100;return 0;}
Mic& Mic::Instance() {static Mic m;return m;}
int Mic::Initialize() {k_mutex_init(&mLock);mHealthy=true;mFeatures={.01f,.01f,0,testUptimeMs,true};return 0;}
SoundFeatures Mic::LatestFeatures() const {auto result=mFeatures;result.timestampMs=testUptimeMs;return result;}
bool Mic::Healthy() const {return mHealthy;}
MatterBridge::MatterBridge() {k_mutex_init(&mLock);}
MatterBridge& MatterBridge::Instance() {static MatterBridge b;return b;}
int MatterBridge::Initialize() {return 0;}
int MatterBridge::StartServer() {return 0;}
int MatterBridge::InitializeRootNode() {return 0;}
int MatterBridge::InitializeAggregator() {return 0;}
int MatterBridge::CreateChildEndpoint(Device&,const char*) {return 0;}
int MatterBridge::PublishDeviceState(Device&) {return 0;}
int MatterBridge::FactoryReset() {return 0;}
ThreadRest& ThreadRest::Instance() {static ThreadRest r;return r;}
int ThreadRest::Initialize() {return 0;}

int main() {
    auto& system=System::Instance(); auto& fan=Fan::Instance();
    assert(Memory::Instance().Initialize()==0 && fan.Initialize()==0);
    assert(LightDevice::Instance().Initialize()==0 && FanDevice::Instance().Initialize()==0);
    Algorithm::Instance().Initialize(Memory::Instance().Config());
    system.mReady=true;
    assert(system.SetManualLight(true,60,250)==0 && system.SetManualFan(true,60)==0);
    // End-of-boost failure with an unchanged logical target used to never retry.
    testUptimeMs=1350; hostPwmFailCall=hostPwmCallCount+1;
    assert(host_run_delayable(hostLastScheduledWork));
    assert(!fan.GetOutputState().valid && system.mTarget.fanPercent==system.mAppliedTarget.fanPercent);
    system.ActuatorRampWork();
    assert(fan.GetOutputState().valid && fan.GetOutputState().appliedDutyPercent==60);

    // Pause a real actuator write and submit a manual command concurrently.
    system.SetMode(ControlMode::Auto); system.mTarget.brightnessPercent=30;
    pauseNextLed=true;
    auto ramp=std::async(std::launch::async,[&]{system.ActuatorRampWork();});
    {std::unique_lock<std::mutex> lock(ledLock); assert(ledSignal.wait_for(lock,std::chrono::seconds(2),[]{return ledBlocked;}));}
    auto manual=std::async(std::launch::async,[&]{return system.SetManualLight(false,0,250);});
    const bool serialized=manual.wait_for(std::chrono::milliseconds(100))==std::future_status::timeout;
    {std::lock_guard<std::mutex> lock(ledLock);ledReleased=true;ledSignal.notify_all();}
    ramp.get(); assert(manual.get()==0 && serialized);
    assert(system.Mode()==ControlMode::Manual && !system.Target().lightOn && !LightDevice::Instance().CurrentState().on);
    system.AlgorithmWork();system.ActuatorRampWork();
    assert(!system.Target().lightOn && !system.mAppliedTarget.lightOn);
    ControlMode storedMode{};ControlTarget stored{};
    assert(Memory::Instance().LoadRuntimeState(&storedMode,&stored)==0 && storedMode==ControlMode::Manual && !stored.lightOn);
    assert(system.SetManualFan(false,0)==0); system.AlgorithmWork();system.ActuatorRampWork();
    assert(!system.Target().lightOn && !system.Target().fanOn && fan.GetOutputState().appliedDutyPercent==0);
    // Restart restores a saved manual off state rather than switching to AUTO.
    system.mMode=ControlMode::Auto; system.mTarget.lightOn=true; system.mTarget.fanOn=true;
    assert(system.RestoreRuntimeState()==0);
    assert(system.Mode()==ControlMode::Manual && !system.Target().lightOn && !system.Target().fanOn);
    // Override expiration resets the shared episode before computing a new auto target.
    system.mSnapshot={100,{.01f,.01f,0,testUptimeMs,true},true,0,true,true,true};
    system.SetMode(ControlMode::Override,1000);
    testUptimeMs+=750;system.mSnapshot.sound.timestampMs=testUptimeMs;system.AlgorithmWork();assert(system.Mode()==ControlMode::Override);
    testUptimeMs+=250;system.mSnapshot.sound.timestampMs=testUptimeMs;system.AlgorithmWork();
    assert(system.Mode()==ControlMode::Auto && system.Target().brightnessPercent==60 && system.Target().fanPercent==60);
    assert(system.SetManualFan(true,60)==0);
    system.SetSafeState();
    assert(system.Mode()==ControlMode::Safe && !system.Target().fanOn && !system.mAppliedTarget.fanOn);
    assert(fan.GetOutputState().valid && fan.GetOutputState().appliedDutyPercent==0);
    puts("production runtime manual/auto serialization, asynchronous PWM retry, manual-off restart, override expiration and physical safe shutdown passed");
}
