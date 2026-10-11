#pragma once
#include <stddef.h>
#include <stdint.h>
#include "zephyr/device.h"

struct pwm_dt_spec { const device* dev; uint32_t channel; uint32_t period; uint32_t flags; };
#define PWM_DT_SPEC_GET(node) {nullptr, 0, 100, 0}

extern int hostPwmCallCount;
extern int hostPwmFailCall;
extern uint8_t hostPwmDuties[64];
inline bool pwm_is_ready_dt(const pwm_dt_spec*) { return true; }
inline int pwm_set_dt(const pwm_dt_spec* spec, uint32_t period, uint32_t pulse) {
    ++hostPwmCallCount;
    if (hostPwmCallCount == hostPwmFailCall) return -5;
    if (hostPwmCallCount <= 64) hostPwmDuties[hostPwmCallCount - 1] =
        period == 0 ? 0 : static_cast<uint8_t>(pulse * 100U / spec->period);
    return 0;
}
