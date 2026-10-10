#pragma once

#include "common/device/device.h"

struct LightState {
    bool on;
    uint8_t brightnessPercent;
    uint16_t cctMireds;
    bool rgbMode;
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    bool operator!=(const LightState& other) const {
        return on != other.on || brightnessPercent != other.brightnessPercent || cctMireds != other.cctMireds ||
               rgbMode != other.rgbMode || red != other.red || green != other.green || blue != other.blue;
    }
};

class LightDevice final : public Device {
  public:
    static LightDevice& Instance();

    int Initialize() override;
    int CreateEndpoint() override;
    int UpdateToMatter(bool force = false) override;
    int ApplyCommand(const DeviceCommand& command) override;
    int ApplyTarget(bool on, uint8_t brightnessPercent, uint16_t cctMireds);
    int ApplyTarget(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red, uint8_t green,
                    uint8_t blue);
    int ApplyTargetNoMatter(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                            uint8_t green, uint8_t blue);
    LightState CurrentState() const {
        return mCurrentState;
    }

    LightDevice(const LightDevice&) = delete;
    LightDevice& operator=(const LightDevice&) = delete;

  private:
    LightDevice();
    ~LightDevice() override = default;

    uint8_t MiredsToCoolPercent(uint16_t mireds) const;
    int ApplyTargetInternal(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                            uint8_t green, uint8_t blue, bool publishMatter);

    LightState mCurrentState = {};
    LightState mPreviousPublished = {};
    uint8_t mLastOnBrightnessPercent = 50;
};

inline LightDevice* GetLightDevice() {
    return &LightDevice::Instance();
}
