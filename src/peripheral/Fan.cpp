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
    k_mutex_lock(&mLock, K_FOREVER);
    if (!mBoosting) {
        k_mutex_unlock(&mLock);
        return;
    }
    mBoosting = false;
    const int ret = pwm_set_dt(&fanPwm, fanPwm.period, fanPwm.period * mBoostTargetPercent / 100U);
    if (ret != 0) {
        mOutput.boosting = false;
        mOutput.valid = false;
        mOutput.lastError = ret;
        k_mutex_unlock(&mLock);
        LOG_ERR("Fan PWM failed: %d", ret);
        return;
    }
    mWasStopped = mBoostTargetPercent == 0U;
    mOutput.appliedDutyPercent = mBoostTargetPercent;
    mOutput.boosting = false;
    mOutput.valid = true;
    mOutput.lastError = 0;
    mOutput.changedAtMs = k_uptime_get_32();
    k_mutex_unlock(&mLock);
#endif
}

int Fan::Initialize() {
    k_mutex_init(&mLock);
    k_work_init_delayable(&mBoostWork, BoostWorkHandler);
#if DT_NODE_HAS_STATUS(DT_ALIAS(fan_pwm), okay)
    if (!pwm_is_ready_dt(&fanPwm)) {
        LOG_ERR("PWM not ready");
        return -ENODEV;
    }

    mState = {false, 0};
    mOutput = {0, 0, false, false, 0, k_uptime_get_32()};
    mWasStopped = true;
    mBoostTargetPercent = 0U;
    mBoosting = false;
    const int ret = Apply(0U);
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
    k_work_cancel_delayable_sync(&mBoostWork, &mBoostCancelSync);
    k_mutex_lock(&mLock, K_FOREVER);
    mBoosting = false;
    mState.on = on;
    if (!on) {
        mState.speedPercent = 0;
    }
    const uint8_t duty = mState.on ? mState.speedPercent : 0U;
    k_mutex_unlock(&mLock);
    return Apply(duty);
}

int Fan::SetSpeed(uint8_t percent) {
    if (percent == 0U) {
        k_work_cancel_delayable_sync(&mBoostWork, &mBoostCancelSync);
    }
    k_mutex_lock(&mLock, K_FOREVER);
    if (percent == 0U) mBoosting = false;
    mState.speedPercent = ClampValue<uint8_t>(percent, 0, 100);
    mState.on = mState.speedPercent > 0U;
    const uint8_t duty = mState.speedPercent;
    k_mutex_unlock(&mLock);
    return Apply(duty);
}

int Fan::Apply(uint8_t duty) {
#if DT_NODE_HAS_STATUS(DT_ALIAS(fan_pwm), okay)
    if (duty > 0U && duty < FAN_MIN_DUTY_PERCENT) {
        duty = FAN_MIN_DUTY_PERCENT;
    }

    k_mutex_lock(&mLock, K_FOREVER);
    mOutput.requestedPercent = duty;
    if (mBoosting) {
        mBoostTargetPercent = duty;
        mOutput.boosting = true;
        k_mutex_unlock(&mLock);
        return 0;
    }

    if (mWasStopped && duty > 0U) {
        int ret = pwm_set_dt(&fanPwm, fanPwm.period, fanPwm.period);
        if (ret != 0) {
            mOutput.valid = false;
            mOutput.lastError = ret;
            k_mutex_unlock(&mLock);
            LOG_ERR("Fan boost failed: %d", ret);
            return ret;
        }
        mBoostTargetPercent = duty;
        mBoosting = true;
        mWasStopped = false;
        mOutput.appliedDutyPercent = 100U;
        mOutput.boosting = true;
        mOutput.valid = true;
        mOutput.lastError = 0;
        mOutput.changedAtMs = k_uptime_get_32();
        k_work_schedule(&mBoostWork, K_MSEC(FAN_START_BOOST_MS));
        k_mutex_unlock(&mLock);
        return 0;
    }

    const int ret = pwm_set_dt(&fanPwm, fanPwm.period, fanPwm.period * duty / 100U);
    if (ret != 0) {
        mOutput.valid = false;
        mOutput.lastError = ret;
        k_mutex_unlock(&mLock);
        LOG_ERR("Fan PWM failed: %d", ret);
        return ret;
    }

    mWasStopped = duty == 0U;
    mOutput.appliedDutyPercent = duty;
    mOutput.boosting = false;
    mOutput.valid = true;
    mOutput.lastError = 0;
    mOutput.changedAtMs = k_uptime_get_32();
    k_mutex_unlock(&mLock);
    return 0;
#else
    return -ENODEV;
#endif
}

Fan::State Fan::GetState() const {
    k_mutex_lock(&mLock, K_FOREVER);
    const State state = mState;
    k_mutex_unlock(&mLock);
    return state;
}

Fan::OutputState Fan::GetOutputState() const {
    k_mutex_lock(&mLock, K_FOREVER);
    const OutputState state = mOutput;
    k_mutex_unlock(&mLock);
    return state;
}
