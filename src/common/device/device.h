#pragma once

#include <stdint.h>

enum class DeviceCommandType : uint8_t {
    None = 0,
    SetPower,
    SetBrightness,
    SetCCT,
    SetFanSpeed,
};

struct DeviceCommand {
    DeviceCommandType type = DeviceCommandType::None;
    bool power = false;
    uint8_t percent = 0;
    uint16_t mireds = 0;
};

class Device {
  public:
    virtual ~Device() = default;
    virtual int Initialize() = 0;
    virtual int CreateEndpoint() = 0;
    virtual int UpdateToMatter(bool force = false) = 0;
    virtual int ApplyCommand(const DeviceCommand& command) = 0;

    uint32_t LogicalId() const {
        return mLogicalId;
    }
    uint16_t EndpointId() const {
        return mEndpointId;
    }
    void SetEndpointId(uint16_t endpointId) {
        mEndpointId = endpointId;
    }
    bool MatterDirty() const {
        return mMatterDirty;
    }

  protected:
    explicit Device(uint32_t logicalId);
    void MarkMatterDirty() {
        mMatterDirty = true;
    }
    void ClearMatterDirty() {
        mMatterDirty = false;
    }

    uint32_t mLogicalId;
    uint16_t mEndpointId = 0;
    bool mMatterDirty = true;
};
