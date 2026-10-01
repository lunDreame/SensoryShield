#include "peripheral/Fan.h"

#include "definition.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fan_peripheral, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(fan_pwm), okay)
static const struct pwm_dt_spec fanPwm = PWM_DT_SPEC_GET(DT_ALIAS(fan_pwm));
#endif

Fan& Fan::Instance() {
    static Fan instance;
    return instance;
}

void Fan::BoostWorkHandler(struct k_work* work) {
    auto* self = CONTAINER_OF(k_work_delayable_from_work(work), Fan, mBoostWork);
    self->FinishStartBoost();
}

void Fan::FinishStartBoost() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(fan_pwm), okay)
    mBoosting = false;
    const int ret = pwm_set_dt(&fanPwm, fanPwm.period, fanPwm.period * mBoostTargetPercent / 100U);
    if (ret != 0) {
        LOG_ERR("Fan PWM failed: %d", ret);
        return;
    }
    mWasStopped = mBoostTargetPercent == 0U;
#endif
}

int Fan::Initialize() {
    k_work_init_delayable(&mBoostWork, BoostWorkHandler);
#if DT_NODE_HAS_STATUS(DT_ALIAS(fan_pwm), okay)
    if (!pwm_is_ready_dt(&fanPwm)) {
        LOG_ERR("PWM not ready");
        return -ENODEV;
    }

    mState = {false, 0};
    const int ret = Apply();
    if (ret != 0) {
        return ret;
    }
    LOG_INF("Fan PWM ready");
    return 0;
#else
    LOG_WRN("Fan PWM alias missing");
    return -ENODEV;
#endif
}

int Fan::SetPower(bool on) {
    k_work_cancel_delayable(&mBoostWork);
    mBoosting = false;
    mState.on = on;
    if (!on) {
        mState.speedPercent = 0;
    }
    return Apply();
}

int Fan::SetSpeed(uint8_t percent) {
    if (percent == 0U) {
        k_work_cancel_delayable(&mBoostWork);
        mBoosting = false;
    }
    mState.speedPercent = ClampValue<uint8_t>(percent, 0, 100);
    mState.on = mState.speedPercent > 0U;
    return Apply();
}

int Fan::Apply() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(fan_pwm), okay)
    uint8_t duty = mState.on ? mState.speedPercent : 0U;
    if (duty > 0U && duty < FAN_MIN_DUTY_PERCENT) {
        duty = FAN_MIN_DUTY_PERCENT;
    }

    if (mBoosting) {
        mBoostTargetPercent = duty;
        return 0;
    }

    if (mWasStopped && duty > 0U) {
        int ret = pwm_set_dt(&fanPwm, fanPwm.period, fanPwm.period);
        if (ret != 0) {
            LOG_ERR("Fan boost failed: %d", ret);
            return ret;
        }
        mBoostTargetPercent = duty;
        mBoosting = true;
        mWasStopped = false;
        k_work_schedule(&mBoostWork, K_MSEC(FAN_START_BOOST_MS));
        return 0;
    }

    const int ret = pwm_set_dt(&fanPwm, fanPwm.period, fanPwm.period * duty / 100U);
    if (ret != 0) {
        LOG_ERR("Fan PWM failed: %d", ret);
        return ret;
    }

    mWasStopped = duty == 0U;
    return 0;
#else
    return -ENODEV;
#endif
}
