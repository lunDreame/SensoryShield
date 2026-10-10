#pragma once

#include "common/device/device.h"

struct FanState {
    bool on;
    uint8_t speedPercent;

    bool operator!=(const FanState& other) const {
        return on != other.on || speedPercent != other.speedPercent;
    }
};

class FanDevice final : public Device {
  public:
    static FanDevice& Instance();

    int Initialize() override;
    int CreateEndpoint() override;
    int UpdateToMatter(bool force = false) override;
    int ApplyCommand(const DeviceCommand& command) override;
    int ApplyTarget(bool on, uint8_t speedPercent);
    int ApplyTargetNoMatter(bool on, uint8_t speedPercent);
    FanState CurrentState() const {
        return mCurrentState;
    }

    FanDevice(const FanDevice&) = delete;
    FanDevice& operator=(const FanDevice&) = delete;

  private:
    FanDevice();
    ~FanDevice() override = default;

    int ApplyTargetInternal(bool on, uint8_t speedPercent, bool publishMatter);

    FanState mCurrentState = {};
    FanState mPreviousPublished = {};
};

inline FanDevice* GetFanDevice() {
    return &FanDevice::Instance();
}
