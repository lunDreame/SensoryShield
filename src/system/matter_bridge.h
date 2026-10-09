#pragma once

#include "common/device/device.h"

#include <platform/CHIPDeviceEvent.h>
#include <zephyr/kernel.h>

class MatterBridge final {
  public:
    static MatterBridge& Instance();

    int Initialize();
    int StartServer();
    void Dispatch();
    int InitializeRootNode();
    int InitializeAggregator();
    int CreateChildEndpoint(Device& device, const char* label);
    int PublishDeviceState(Device& device);
    int FactoryReset();

    bool Ready() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const bool ready = mReady;
        k_mutex_unlock(&mLock);
        return ready;
    }
    bool Started() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const bool started = mStarted;
        k_mutex_unlock(&mLock);
        return started;
    }
    bool Commissioned() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const bool commissioned = mCommissioned;
        k_mutex_unlock(&mLock);
        return commissioned;
    }
    bool ThreadAttached() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const bool threadAttached = mThreadAttached;
        k_mutex_unlock(&mLock);
        return threadAttached;
    }
    bool CommissioningActive() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const bool commissioningActive = mCommissioningActive;
        k_mutex_unlock(&mLock);
        return commissioningActive;
    }
    uint8_t FabricCount() const {
        k_mutex_lock(&mLock, K_FOREVER);
        const uint8_t fabricCount = mFabricCount;
        k_mutex_unlock(&mLock);
        return fabricCount;
    }

    MatterBridge(const MatterBridge&) = delete;
    MatterBridge& operator=(const MatterBridge&) = delete;

  private:
    MatterBridge();
    ~MatterBridge() = default;
    static void HandleEvent(const chip::DeviceLayer::ChipDeviceEvent* event, intptr_t arg);

    mutable struct k_mutex mLock;
    bool mReady = false;
    bool mStarted = false;
    bool mCommissioned = false;
    bool mCommissioningActive = false;
    bool mThreadAttached = false;
    uint8_t mFabricCount = 0;
};

inline MatterBridge* GetMatterBridge() {
    return &MatterBridge::Instance();
}
