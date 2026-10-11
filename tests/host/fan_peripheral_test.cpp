#include "peripheral/Fan.h"
#include "definition.h"
#include <cassert>
#include <array>
#include <cstdio>

uint32_t testUptimeMs = 0;
int hostPwmCallCount = 0;
int hostPwmFailCall = -1;
uint8_t hostPwmDuties[64] = {};

static void resetPwm(int failCall = -1) {
    hostPwmCallCount = 0; hostPwmFailCall = failCall;
    for (auto& duty : hostPwmDuties) duty = 0;
}

int main() {
    auto& fan = Fan::Instance();
    resetPwm(); assert(fan.Initialize() == 0); assert(hostPwmDuties[0] == 0);

    testUptimeMs = 10; assert(fan.SetSpeed(20) == 0);
    auto output = fan.GetOutputState();
    assert(output.requestedPercent == 20 && output.appliedDutyPercent == 100);
    assert(output.boosting && output.valid && output.changedAtMs == 10);
    assert(hostPwmDuties[1] == 100);
    testUptimeMs = 100; assert(fan.SetSpeed(60) == 0);
    output = fan.GetOutputState(); assert(output.requestedPercent == 60 && output.appliedDutyPercent == 100);
    testUptimeMs = 360; assert(host_run_delayable(hostLastScheduledWork));
    output = fan.GetOutputState();
    assert(!output.boosting && output.valid && output.appliedDutyPercent == 60 && output.changedAtMs == 360);
    assert(hostPwmDuties[2] == 60);

    // OFF cancels a pending boost callback, and a stale host callback cannot overwrite OFF.
    assert(fan.SetSpeed(0) == 0);
    assert(fan.SetSpeed(20) == 0); auto* cancelled = hostLastScheduledWork;
    assert(fan.SetSpeed(0) == 0); const int callsAfterOff = hostPwmCallCount;
    assert(!host_run_delayable(cancelled) && hostPwmCallCount == callsAfterOff);
    output = fan.GetOutputState(); assert(output.valid && !output.boosting && output.appliedDutyPercent == 0);
    assert(fan.SetSpeed(20) == 0); auto* restarted = hostLastScheduledWork;
    assert(host_run_delayable(restarted));
    output = fan.GetOutputState(); assert(output.valid && !output.boosting && output.appliedDutyPercent == 20);
    assert(fan.SetSpeed(0) == 0);

    // Starting failure is synchronous and does not claim that 100% was applied.
    resetPwm(1); assert(fan.Initialize() < 0);
    resetPwm(); assert(fan.Initialize() == 0); hostPwmFailCall = 2;
    assert(fan.SetSpeed(20) < 0);
    output = fan.GetOutputState(); assert(!output.valid && output.appliedDutyPercent == 0);

    // End-of-boost failure is visible asynchronously; repeating the target retries it.
    hostPwmFailCall = -1; assert(fan.SetSpeed(20) == 0);
    hostPwmFailCall = hostPwmCallCount + 1;
    assert(host_run_delayable(hostLastScheduledWork));
    output = fan.GetOutputState(); assert(!output.valid && !output.boosting && output.appliedDutyPercent == 100);
    hostPwmFailCall = -1; assert(fan.SetSpeed(20) == 0);
    output = fan.GetOutputState(); assert(output.valid && output.appliedDutyPercent == 20);

    assert(fan.SetSpeed(0) == 0);
    for (uint8_t requested : std::array<uint8_t, 6>{1, 17, 18, 20, 60, 100}) {
        assert(fan.SetSpeed(requested) == 0);
        output = fan.GetOutputState(); assert(output.appliedDutyPercent == 100 && output.boosting);
        assert(host_run_delayable(hostLastScheduledWork));
        const uint8_t expected = requested < 18 ? 18 : requested;
        assert(fan.GetOutputState().appliedDutyPercent == expected);
        assert(fan.SetSpeed(0) == 0);
    }
    assert(NormalizeFanPercent(60, 0) == 0);
    assert(NormalizeFanPercent(60, 10) == 0);
    assert(NormalizeFanPercent(1, 18) == 18);
    assert(NormalizeFanPercent(17, 100) == 18);
    assert(NormalizeFanPercent(20, 18) == 18);
    assert(NormalizeFanPercent(60, 100) == 60);
    assert(NormalizeFanPercent(100, 100) == 100);
    assert(ElapsedUptimeMs(149U, UINT32_MAX - 100U) == 250U);
    puts("fan peripheral boost, cancellation, failures, duty bounds and caps passed");
}
