#include "common/device/Occupancy.h"

#include "definition.h"
#include "peripheral/PIR.h"
#include "system/matter_bridge.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(occupancy_device, LOG_LEVEL_INF);

OccupancyDevice::OccupancyDevice() : Device(3) {}

OccupancyDevice& OccupancyDevice::Instance() {
    static OccupancyDevice instance;
    return instance;
}

int OccupancyDevice::Initialize() {
    mPreviousOccupied = false;
    MarkMatterDirty();
    return 0;
}

int OccupancyDevice::CreateEndpoint() {
    if (mEndpointId == 0U) {
        mEndpointId = 5;
    }
    const int ret = GetMatterBridge()->CreateChildEndpoint(*this, "재실 센서");
    if (ret != 0) {
        return ret;
    }
    LOG_INF("Child ready: Occupancy ep=%u", mEndpointId);
    return 0;
}

int OccupancyDevice::UpdateToMatter(bool force) {
    const bool current = GetPIR()->IsOccupied();
    if (!force && !MatterDirty() && !HasChanged(mPreviousOccupied, current)) {
        return 0;
    }

    const int ret = GetMatterBridge()->PublishDeviceState(*this);
    if (ret != 0) {
        MarkMatterDirty();
        return ret;
    }

    mPreviousOccupied = current;
    ClearMatterDirty();
    LOG_INF("Occupancy published: %s", current ? "occupied" : "clear");
    return 0;
}

int OccupancyDevice::ApplyCommand(const DeviceCommand& command) {
    ARG_UNUSED(command);
    return 0;
}
