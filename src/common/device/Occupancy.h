#pragma once

#include "common/device/device.h"

class OccupancyDevice final : public Device {
  public:
    static OccupancyDevice& Instance();

    int Initialize() override;
    int CreateEndpoint() override;
    int UpdateToMatter(bool force = false) override;
    int ApplyCommand(const DeviceCommand& command) override;

    OccupancyDevice(const OccupancyDevice&) = delete;
    OccupancyDevice& operator=(const OccupancyDevice&) = delete;

  private:
    OccupancyDevice();
    ~OccupancyDevice() override = default;

    bool mPreviousOccupied = false;
};

inline OccupancyDevice* GetOccupancyDevice() {
    return &OccupancyDevice::Instance();
}
