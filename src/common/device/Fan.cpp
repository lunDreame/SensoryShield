#include "common/device/Fan.h"

#include "definition.h"
#include "peripheral/Fan.h"
#include "system/matter_bridge.h"
#include "system/memory.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fan_device, LOG_LEVEL_INF);

FanDevice::FanDevice() : Device(2) {}

FanDevice& FanDevice::Instance() {
    static FanDevice instance;
    return instance;
}

int FanDevice::Initialize() {
    mCurrentState = {false, 0};
    mPreviousPublished = mCurrentState;
    MarkMatterDirty();
    return 0;
}

int FanDevice::CreateEndpoint() {
    if (mEndpointId == 0U) {
        mEndpointId = 4;
    }
    const int ret = GetMatterBridge()->CreateChildEndpoint(*this, "팬");
    if (ret != 0) {
        return ret;
    }
    LOG_INF("Child ready: Fan ep=%u", mEndpointId);
    return 0;
}

int FanDevice::UpdateToMatter(bool force) {
    if (!force && !MatterDirty() && !HasChanged(mPreviousPublished, mCurrentState)) {
        return 0;
    }

    const int ret = GetMatterBridge()->PublishDeviceState(*this);
    if (ret != 0) {
        MarkMatterDirty();
        return ret;
    }

    mPreviousPublished = mCurrentState;
    ClearMatterDirty();
    LOG_INF("Fan changed: on=%d speed=%u", mCurrentState.on, mCurrentState.speedPercent);
    return 0;
}

int FanDevice::ApplyCommand(const DeviceCommand& command) {
    switch (command.type) {
    case DeviceCommandType::SetPower:
        return ApplyTarget(command.power, command.power ? mCurrentState.speedPercent : 0);
    case DeviceCommandType::SetFanSpeed:
        return ApplyTarget(command.percent > 0U, command.percent);
    default:
        return 0;
    }
}

int FanDevice::ApplyTarget(bool on, uint8_t speedPercent) {
    const uint8_t maxPercent = GetMemory()->Config().fanMaxPercent;
    const FanState next = {on, static_cast<uint8_t>(on ? ClampValue<uint8_t>(speedPercent, 0, maxPercent) : 0)};

    if (!HasChanged(mCurrentState, next)) {
        return 0;
    }

    const int ret = GetFan()->SetSpeed(next.speedPercent);
    if (ret != 0) {
        return ret;
    }

    mCurrentState = next;
    MarkMatterDirty();
    return UpdateToMatter();
}
