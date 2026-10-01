#pragma once

#include <zephyr/kernel.h>
#include <stdint.h>

class Fan final {
  public:
    struct State {
        bool on;
        uint8_t speedPercent;
    };

    static Fan& Instance();

    int Initialize();
    int SetPower(bool on);
    int SetSpeed(uint8_t percent);
    State GetState() const {
        return mState;
    }

    Fan(const Fan&) = delete;
    Fan& operator=(const Fan&) = delete;

  private:
    Fan() = default;
    ~Fan() = default;

    int Apply();
    static void BoostWorkHandler(struct k_work* work);
    void FinishStartBoost();

    State mState = {};
    bool mWasStopped = true;
    uint8_t mBoostTargetPercent = 0;
    bool mBoosting = false;
    struct k_work_delayable mBoostWork;
};

inline Fan* GetFan() {
    return &Fan::Instance();
}
