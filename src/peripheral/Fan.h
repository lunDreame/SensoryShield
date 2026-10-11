#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

class Fan final {
  public:
    struct State {
        bool on;
        uint8_t speedPercent;
    };
    struct OutputState {
        uint8_t requestedPercent;
        uint8_t appliedDutyPercent;
        bool boosting;
        bool valid;
        int lastError;
        uint32_t changedAtMs;
    };

    static Fan& Instance();

    int Initialize();
    int SetPower(bool on);
    int SetSpeed(uint8_t percent);
    State GetState() const;
    OutputState GetOutputState() const;

    Fan(const Fan&) = delete;
    Fan& operator=(const Fan&) = delete;

  private:
    Fan() = default;
    ~Fan() = default;

    int Apply(uint8_t duty);
    static void BoostWorkHandler(struct k_work* work);
    void FinishStartBoost();

    State mState = {};
    OutputState mOutput = {};
    bool mWasStopped = true;
    uint8_t mBoostTargetPercent = 0;
    bool mBoosting = false;
    struct k_work_delayable mBoostWork;
    struct k_work_sync mBoostCancelSync;
    mutable struct k_mutex mLock;
};

inline Fan* GetFan() {
    return &Fan::Instance();
}
