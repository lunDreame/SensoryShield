#include "peripheral/CCTLight.h"

#include "definition.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cct_peripheral, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(warm_pwm), okay)
static const struct pwm_dt_spec warmPwm = PWM_DT_SPEC_GET(DT_ALIAS(warm_pwm));
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(cool_pwm), okay)
static const struct pwm_dt_spec coolPwm = PWM_DT_SPEC_GET(DT_ALIAS(cool_pwm));
#endif

CCTLight& CCTLight::Instance() {
    static CCTLight instance;
    return instance;
}

int CCTLight::Initialize() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(warm_pwm), okay) && DT_NODE_HAS_STATUS(DT_ALIAS(cool_pwm), okay)
    if (!pwm_is_ready_dt(&warmPwm) || !pwm_is_ready_dt(&coolPwm)) {
        LOG_ERR("PWM not ready");
        return -ENODEV;
    }

    mState = {false, 0, 50};
    const int ret = Apply();
    if (ret != 0) {
        return ret;
    }
    LOG_INF("CCT PWM ready");
    return 0;
#else
    LOG_WRN("CCT PWM aliases missing");
    return -ENODEV;
#endif
}

int CCTLight::SetPower(bool on) {
    mState.on = on;
    return Apply();
}

int CCTLight::SetBrightness(uint8_t percent) {
    mState.brightnessPercent = ClampValue<uint8_t>(percent, 0, 100);
    return Apply();
}

int CCTLight::SetCCTRatio(uint8_t coolPercent) {
    mState.coolPercent = ClampValue<uint8_t>(coolPercent, 0, 100);
    return Apply();
}

int CCTLight::Apply() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(warm_pwm), okay) && DT_NODE_HAS_STATUS(DT_ALIAS(cool_pwm), okay)
    const uint32_t brightness = mState.on ? mState.brightnessPercent : 0U;
    const uint32_t warmDuty = brightness * (100U - mState.coolPercent) / 100U;
    const uint32_t coolDuty = brightness * mState.coolPercent / 100U;

    int ret = pwm_set_dt(&warmPwm, warmPwm.period, warmPwm.period * warmDuty / 100U);
    if (ret != 0) {
        LOG_ERR("Warm PWM failed: %d", ret);
        return ret;
    }

    ret = pwm_set_dt(&coolPwm, coolPwm.period, coolPwm.period * coolDuty / 100U);
    if (ret != 0) {
        LOG_ERR("Cool PWM failed: %d", ret);
        return ret;
    }

    return 0;
#else
    return -ENODEV;
#endif
}
