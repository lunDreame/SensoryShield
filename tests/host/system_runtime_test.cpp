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
    // The first power-on uses the normal tone, not the warm relief/range limit.
    // Saved normal tones still take precedence over the 4,000 K default.
    const AppConfig defaults=Memory::Instance().Config();
    assert(defaults.normalCCTMireds==250 && defaults.preferredCCTMireds==454);
    for (uint16_t tone : {250, 333}) {
        AppConfig configured=defaults; configured.normalCCTMireds=tone;
        assert(Memory::Instance().SaveConfig(configured)==0);
        assert(LightDevice::Instance().Initialize()==0);
        DeviceCommand power{}; power.type=DeviceCommandType::SetPower; power.power=true;
        assert(LightDevice::Instance().ApplyCommand(power)==0);
        const auto first=LightDevice::Instance().CurrentState();
        assert(first.on && !first.rgbMode && first.cctMireds==tone);
    }
    assert(Memory::Instance().SaveConfig(defaults)==0);
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
    assert(system.Mode()==ControlMode::Auto && system.Target().brightnessPercent==DEFAULT_NORMAL_BRIGHTNESS && system.Target().fanPercent==DEFAULT_NORMAL_FAN);
    assert(system.SetManualFan(true,60)==0);
    system.SetSafeState();
    assert(system.Mode()==ControlMode::Safe && !system.Target().fanOn && !system.mAppliedTarget.fanOn);
    assert(fan.GetOutputState().valid && fan.GetOutputState().appliedDutyPercent==0);
    // Exercise the real child endpoint target path and scheduler, not browser fixtures.
    AppConfig demo=Memory::Instance().Config();
    demo.maxBrightness=demo.preferredBrightness=demo.preferredFanPercent=100;
    demo.reliefHoldMs=5000;demo.preferredCCTMireds=454;
    assert(system.UpdateConfig(demo)==0);
    assert(system.SetManualLight(true,100,250)==0 && system.SetManualFan(true,100)==0);
    auto boost=hostLastScheduledWork;testUptimeMs+=350;host_run_delayable(boost);
    system.SetMode(ControlMode::Auto);
    auto run=[&](uint32_t duration,float rms=.01f) {
        for(uint32_t t=0;t<duration;t+=25) {
            testUptimeMs+=25;
            if(t%200==0) {
                const auto light=LightDevice::Instance().CurrentState();
                const auto motor=fan.GetOutputState();
                system.mSnapshot={100,{rms,rms,0,testUptimeMs,true},true,testUptimeMs,true,true,true};
                system.mSnapshot.observedAtMs=testUptimeMs;
                system.mSnapshot.observedOutputs={true,light.on,light.brightnessPercent,light.cctMireds,light.rgbMode,
                    light.red,light.green,light.blue,motor.appliedDutyPercent>0,motor.appliedDutyPercent,
                    light.brightnessPercent==system.mTarget.brightnessPercent && light.cctMireds==system.mTarget.cctMireds,
                    motor.appliedDutyPercent==system.mTarget.fanPercent};
                system.mSnapshot.observedOutputs.fanOutputValid=motor.valid;
                system.mSnapshot.observedOutputs.fanBoosting=motor.boosting;
            }
            if(t%250==0) system.AlgorithmWork();
            system.ActuatorRampWork();
        }
    };
    run(15000);assert(system.EnvironmentBaselineReady());
    assert(system.RequestComfort(ComfortAction::Bright)==0);
    assert(system.Mode()==ControlMode::Override && system.OverrideRemainingSeconds()==CHILD_HOLD_MS/1000);
    assert(system.mAppliedTarget.brightnessPercent==100); // Acceptance does not jump output.
    run(2500);assert(system.mAppliedTarget.brightnessPercent<100 && system.mAppliedTarget.brightnessPercent>30);
    run(3500);
#if DEMO_DEFAULTS
    assert(system.mAppliedTarget.brightnessPercent==30 && system.mAppliedTarget.cctMireds==454 && system.Mode()==ControlMode::Override);
    assert(system.OverrideRemainingSeconds()>=4);
    run(5500);assert(system.Mode()==ControlMode::Auto);
    run(5000);assert(system.mAppliedTarget.brightnessPercent==100 && system.mAppliedTarget.cctMireds==250);
    assert(system.RequestComfort(ComfortAction::Loud)==0);
    run(20000,.08f);assert(system.Mode()==ControlMode::Auto);
    assert(system.mAppliedTarget.brightnessPercent==30 && system.mAppliedTarget.fanPercent==demo.reliefFanPercent);
    run(14000);assert(system.mAppliedTarget.brightnessPercent==100 && system.mAppliedTarget.fanPercent==100);
#else
    assert(system.mAppliedTarget.brightnessPercent==70 && system.Mode()==ControlMode::Override);
    run(10000);assert(system.Mode()==ControlMode::Override && system.OverrideRemainingSeconds()>1700);
#endif
    assert(system.RequestComfort(ComfortAction::Bright)==0);
    run(1000);assert(system.SetManualLight(false,0,250)==0);run(17000);
    assert(system.Mode()==ControlMode::Manual && !system.mAppliedTarget.lightOn && !system.mChildRequest);
    assert(system.RequestComfort(ComfortAction::Bright)==-EALREADY); // Off stays off.
    assert(system.RequestComfort(ComfortAction::Dark)==0);run(1000);
    assert(system.UpdateConfig(demo)==0);assert(!system.mChildRequest && system.Mode()==ControlMode::Auto);
    puts("production child request, actual arrival timer, mode expiry, high-input retention, manual cancellation and settings edit passed");
    puts("production runtime manual/auto serialization, asynchronous PWM retry, manual-off restart, override expiration and physical safe shutdown passed");
}
