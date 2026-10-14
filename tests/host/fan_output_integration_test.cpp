#include "common/algorithm/algorithm.h"
#include "peripheral/Fan.h"
#include "system/output_retry.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
uint32_t testUptimeMs=1000;
int hostPwmCallCount=0, hostPwmFailCall=-1;
uint8_t hostPwmDuties[64]={};

// Actual Fan implementation and PWM test double: minimum duty and startup boost
// must be reflected in observed output, rather than assigning the requested speed.
struct Room {
    Algorithm& a=Algorithm::Instance(); Fan& fan=Fan::Instance(); AppConfig c;
    int brightness=60,cct=250; uint32_t elapsed=0, boostStarted=0;
    bool startedBoost=false, failBoostOnce=false; uint32_t lastAttemptMs=0; int retries=0;
    float extraSound=0;
    SensorSnapshot s{}; StimulusScore score{};
    ControlTarget target{true,60,250,false,255,255,255,true,60,0};
    Room(uint8_t normal=60) {
        c.reliefHoldMs=3000; // Short regression fixture, independent of the everyday default.
        c.preferredFanPercent=normal; c.reliefFanPercent=0;
        target.fanPercent=normal; a.Initialize(c); assert(fan.Initialize()==0);
    }
    void run(uint32_t duration) {
        auto end=elapsed+duration;
        while(elapsed<end) {
            testUptimeMs=1000+elapsed;
            if(startedBoost && elapsed-boostStarted>=FAN_START_BOOST_MS) {
                if(failBoostOnce) {hostPwmFailCall=hostPwmCallCount+1; failBoostOnce=false;}
                host_run_delayable(hostLastScheduledWork); startedBoost=false;
            }
            const auto output=fan.GetOutputState();
            if(elapsed%SENSOR_PERIOD_MS==0) {
                const float rms=.01f+.00005f*output.appliedDutyPercent+extraSound;
                s={100+.5f*brightness,{rms,rms,0,testUptimeMs,true},true,0,true,true,true};
                s.observedAtMs=testUptimeMs;
                s.observedOutputs={true,brightness>0,static_cast<uint8_t>(brightness),static_cast<uint16_t>(cct),false,255,255,255,
                    output.appliedDutyPercent>0,output.appliedDutyPercent,
                    brightness==target.brightnessPercent && cct==target.cctMireds,
                    output.valid && !output.boosting && output.appliedDutyPercent==target.fanPercent};
                s.observedOutputs.fanOutputValid=output.valid;
                s.observedOutputs.fanBoosting=output.boosting;
            }
            if(elapsed%ALGORITHM_PERIOD_MS==0) { score=a.EvaluateEnvironment(s); target=a.Evaluate(s,score); }
            brightness+=std::clamp(int(target.brightnessPercent)-brightness,-2,2);
            cct+=std::clamp(int(target.cctMireds)-cct,-8,8);
            // Mirrors FanDevice normalization, while feeding real PWM state back.
            const uint8_t request=NormalizeFanPercent(target.fanOn?target.fanPercent:0,c.fanMaxPercent);
            if(request!=output.requestedPercent || OutputRetryDue(output.valid,testUptimeMs,lastAttemptMs)) {
                lastAttemptMs=testUptimeMs; if(!output.valid) ++retries;
                assert(fan.SetSpeed(request)==0);
                if(!output.boosting && fan.GetOutputState().boosting) {startedBoost=true; boostStarted=elapsed;}
            }
            elapsed+=ACTUATOR_RAMP_MS;
        }
    }
};
int main() {
    assert(!OutputRetryDue(true,1000,0));
    assert(!OutputRetryDue(false,1249,1000));
    assert(OutputRetryDue(false,1250,1000));
    assert(OutputRetryDue(false,149,UINT32_MAX-100));
    for(uint8_t normal : {uint8_t(18),uint8_t(60)}) {
        Room r(normal); r.run(10000); assert(r.score.baselineReady);
        auto baseline=r.a.SoundBaselineMedian();
        r.extraSound=.08f; r.run(20000);
        assert(r.fan.GetOutputState().appliedDutyPercent==0 && r.brightness==30);
        assert(r.a.SoundBaselineMedian()==baseline);
        r.extraSound=0; r.failBoostOnce=true; r.run(120000);
        assert(r.retries==1);
        assert(r.target.fanPercent==normal && r.fan.GetOutputState().appliedDutyPercent==normal);
        assert(r.brightness==60 && r.cct==250 && r.score.baselineReady);
        puts("zero relief fan restores normal output with actual minimum PWM, startup boost and asynchronous-error retry");
    }
}
