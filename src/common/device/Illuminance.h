#pragma once

#include "common/device/device.h"

class IlluminanceDevice final : public Device {
  public:
    static IlluminanceDevice& Instance();

    int Initialize() override;
    int CreateEndpoint() override;
    int UpdateToMatter(bool force = false) override;
    int ApplyCommand(const DeviceCommand& command) override;
    void SetCurrentLux(float lux, bool valid);
    float CurrentLux() const {
        return mCurrentLux;
    }
    bool CurrentLuxValid() const {
        return mValid;
    }

    IlluminanceDevice(const IlluminanceDevice&) = delete;
    IlluminanceDevice& operator=(const IlluminanceDevice&) = delete;

  private:
    IlluminanceDevice();
    ~IlluminanceDevice() override = default;

    float mCurrentLux = 0.0f;
    float mPreviousLux = -1000.0f;
    int64_t mLastPublishMs = -1;
    bool mValid = false;
    bool mPreviousValid = false;
};

inline IlluminanceDevice* GetIlluminanceDevice() {
    return &IlluminanceDevice::Instance();
}
