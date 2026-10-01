#pragma once

#include "common/device/device.h"

#include <platform/CHIPDeviceEvent.h>

class MatterBridge final {
  public:
    static MatterBridge& Instance();

    int Initialize();
    void Dispatch();
    int InitializeRootNode();
    int InitializeAggregator();
    int CreateChildEndpoint(Device& device, const char* label);
    int PublishDeviceState(Device& device);
    int FactoryReset();

    bool Ready() const {
        return mReady;
    }
    bool Commissioned() const {
        return mCommissioned;
    }
    bool ThreadAttached() const {
        return mThreadAttached;
    }
    uint8_t FabricCount() const {
        return mFabricCount;
    }

    MatterBridge(const MatterBridge&) = delete;
    MatterBridge& operator=(const MatterBridge&) = delete;

  private:
    MatterBridge() = default;
    ~MatterBridge() = default;
    static void HandleEvent(const chip::DeviceLayer::ChipDeviceEvent* event, intptr_t arg);

    bool mReady = false;
    bool mCommissioned = false;
    bool mThreadAttached = false;
    uint8_t mFabricCount = 0;
};

inline MatterBridge* GetMatterBridge() {
    return &MatterBridge::Instance();
}
