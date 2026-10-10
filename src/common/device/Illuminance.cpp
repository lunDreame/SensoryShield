#include "common/device/Illuminance.h"

#include "system/matter_bridge.h"

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(illuminance_device, LOG_LEVEL_INF);

namespace {
constexpr float kLuxPublishDelta = 1.0f;
constexpr int64_t kLuxPublishMinIntervalMs = 1000;
} // namespace

IlluminanceDevice::IlluminanceDevice() : Device(4) {}

IlluminanceDevice& IlluminanceDevice::Instance() {
    static IlluminanceDevice instance;
    return instance;
}

int IlluminanceDevice::Initialize() {
    mCurrentLux = 0.0f;
    mPreviousLux = -1000.0f;
    mLastPublishMs = -1;
    mValid = false;
    mPreviousValid = false;
    MarkMatterDirty();
    return 0;
}

int IlluminanceDevice::CreateEndpoint() {
    if (mEndpointId == 0U) {
        mEndpointId = 6;
    }
    const int ret = GetMatterBridge()->CreateChildEndpoint(*this, "조도 센서");
    if (ret != 0) {
        return ret;
    }
    LOG_INF("Child ready: Illuminance ep=%u", mEndpointId);
    return 0;
}

void IlluminanceDevice::SetCurrentLux(float lux, bool valid) {
    const bool wasValid = mValid;
    mCurrentLux = lux;
    mValid = valid;
    if (wasValid != valid || (valid && fabsf(mCurrentLux - mPreviousLux) >= kLuxPublishDelta)) {
        MarkMatterDirty();
    }
}

int IlluminanceDevice::UpdateToMatter(bool force) {
    const bool deltaChanged = mValid && fabsf(mCurrentLux - mPreviousLux) >= kLuxPublishDelta;
    const bool validityChanged = mValid != mPreviousValid;
    if (!force && !MatterDirty() && !deltaChanged && !validityChanged) {
        return 0;
    }
    const int64_t nowMs = k_uptime_get();
    if (!force && !validityChanged && mLastPublishMs >= 0 && (nowMs - mLastPublishMs) < kLuxPublishMinIntervalMs) {
        MarkMatterDirty();
        return 0;
    }

    const int ret = GetMatterBridge()->PublishDeviceState(*this);
    if (ret != 0) {
        MarkMatterDirty();
        return ret;
    }

    if (mValid) {
        mPreviousLux = mCurrentLux;
    }
    mPreviousValid = mValid;
    mLastPublishMs = nowMs;
    ClearMatterDirty();
    const int32_t luxTenths = static_cast<int32_t>((mCurrentLux * 10.0f) + 0.5f);
    LOG_INF("Illuminance published: lux=%d.%u", luxTenths / 10, static_cast<unsigned int>(luxTenths % 10));
    return 0;
}

int IlluminanceDevice::ApplyCommand(const DeviceCommand& command) {
    ARG_UNUSED(command);
    return 0;
}
