#include "common/algorithm/output_aware_inputs.h"
#include "common/algorithm/light_sensor_calibration.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    OutputAwareInputs observer;
    SensorSnapshot s = {150, {.01f,.02f,0,0,true},true,0,true,true,true};
    s.observedOutputs = {true,true,100,250,false,255,255,255,true,60,true,true};
    assert(observer.Observe(s).controlInputsSettled);
    s.observedOutputs.brightness = 25; s.lux = 112.5f;
    s.observedAtMs = s.sound.timestampMs = 250;
    auto changed = observer.Observe(s);
    assert(changed.lux == 112.5f && !changed.learnLight && !changed.controlInputsSettled);
    s.observedAtMs = s.sound.timestampMs = 500;
    assert(!observer.Observe(s).learnLight);
    s.observedAtMs = s.sound.timestampMs = 750;
    assert(observer.Observe(s).controlInputsSettled);
    // The achieved reduction remains visible, including external changes during a ramp.
    s.observedOutputs.brightness = 50; s.lux = 140;
    s.observedOutputs.lightSettled = false;
    s.observedAtMs = s.sound.timestampMs = 1000;
    assert(observer.Observe(s).lux == 140);
    s.observedOutputs.lightSettled = true;
    s.observedAtMs = s.sound.timestampMs = 1250;
    assert(!observer.Observe(s).learnLight);
    s.observedAtMs = s.sound.timestampMs = 1500;
    assert(observer.Observe(s).learnLight);
    s.observedOutputs.fanPercent = 20; s.sound.energy=.002f; s.sound.peak=.004f;
    s.observedAtMs = s.sound.timestampMs = 1750;
    auto fan = observer.Observe(s);
    assert(fan.learnLight && !fan.learnSound && fan.sound.energy == .002f);
    auto duplicate = observer.Observe(s);
    assert(!duplicate.controlInputsSettled);
    s.observedAtMs = s.sound.timestampMs = 2000; s.micValid=false;
    assert(!observer.Observe(s).micValid);
    s.micValid=true; s.learnLight=false; s.observedAtMs=s.sound.timestampMs=3000;
    assert(!observer.Observe(s).learnLight); // Caller admission veto is preserved.
    // Optional calibration remains a separate diagnostic utility, not the default control input.
    LightSensorCalibration calibration{true,50,40,30}; float ambient;
    s.observedOutputs.lightOn=true; s.observedOutputs.brightness=50; s.observedOutputs.cct=333;
    assert(calibration.Correct(120,s.observedOutputs,ambient) && ambient==100);
    assert(!calibration.Correct(NAN,s.observedOutputs,ambient));
    s.observedOutputs.rgb=true; assert(!calibration.Correct(120,s.observedOutputs,ambient));
    OutputAwareSignal wrapping;
    wrapping.Observe(100,1,UINT32_MAX-100,true,true);
    assert(wrapping.Observe(101,1,149,true,true).value==101);
    puts("total room input preserved, transitions gated, invalid/duplicate/wrapped samples handled passed");
}
