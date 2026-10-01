#include "common/device/Light.h"

#include "definition.h"
#include "peripheral/CCTLight.h"
#include "system/matter_bridge.h"
#include "system/memory.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(light_device, LOG_LEVEL_INF);

LightDevice::LightDevice() : Device(1) {}

LightDevice& LightDevice::Instance() {
    static LightDevice instance;
    return instance;
}

int LightDevice::Initialize() {
    const AppConfig config = GetMemory()->Config();
    mCurrentState = {false, 0, config.maxCCTMireds};
    mPreviousPublished = mCurrentState;
    mLastOnBrightnessPercent = 50;
    MarkMatterDirty();
    return 0;
}

int LightDevice::CreateEndpoint() {
    if (mEndpointId == 0U) {
        mEndpointId = 3;
    }
    const int ret = GetMatterBridge()->CreateChildEndpoint(*this, "조명");
    if (ret != 0) {
        return ret;
    }
    LOG_INF("Light child ready: endpoint=%u", mEndpointId);
    return 0;
}

int LightDevice::UpdateToMatter(bool force) {
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
    LOG_INF("Light changed: on=%d brightness=%u cct=%u", mCurrentState.on, mCurrentState.brightnessPercent,
            mCurrentState.cctMireds);
    return 0;
}

int LightDevice::ApplyCommand(const DeviceCommand& command) {
    LightState target = mCurrentState;
    switch (command.type) {
    case DeviceCommandType::SetPower:
        target.on = command.power;
        if (!target.on) {
            if (target.brightnessPercent > 0U) {
                mLastOnBrightnessPercent = target.brightnessPercent;
            }
            target.brightnessPercent = 0;
        } else if (target.brightnessPercent == 0U) {
            target.brightnessPercent = mLastOnBrightnessPercent;
        }
        break;
    case DeviceCommandType::SetBrightness:
        target.brightnessPercent = ClampValue<uint8_t>(command.percent, 0, 100);
        target.on = target.brightnessPercent > 0U;
        break;
    case DeviceCommandType::SetCCT:
        target.cctMireds = command.mireds;
        break;
    default:
        return 0;
    }

    return ApplyTarget(target.on, target.brightnessPercent, target.cctMireds);
}

int LightDevice::ApplyTarget(bool on, uint8_t brightnessPercent, uint16_t cctMireds) {
    const AppConfig config = GetMemory()->Config();
    LightState next = {on, static_cast<uint8_t>(on ? ClampValue<uint8_t>(brightnessPercent, 0, 100) : 0),
                          ClampValue<uint16_t>(cctMireds, config.minCCTMireds, config.maxCCTMireds)};

    int ret = GetCCTLight()->SetCCTRatio(MiredsToCoolPercent(next.cctMireds));
    if (ret == 0) {
        ret = GetCCTLight()->SetBrightness(next.brightnessPercent);
    }
    if (ret == 0) {
        ret = GetCCTLight()->SetPower(next.on);
    }
    if (ret != 0) {
        return ret;
    }

    if (next.on && next.brightnessPercent > 0U) {
        mLastOnBrightnessPercent = next.brightnessPercent;
    }
    mCurrentState = next;
    MarkMatterDirty();
    return UpdateToMatter();
}

uint8_t LightDevice::MiredsToCoolPercent(uint16_t mireds) const {
    const AppConfig config = GetMemory()->Config();
    if (config.maxCCTMireds <= config.minCCTMireds) {
        return 50;
    }

    const uint16_t clamped = ClampValue<uint16_t>(mireds, config.minCCTMireds, config.maxCCTMireds);
    const uint16_t span = config.maxCCTMireds - config.minCCTMireds;
    const uint16_t warmBias = clamped - config.minCCTMireds;
    return static_cast<uint8_t>(100U - ((warmBias * 100U) / span));
}
