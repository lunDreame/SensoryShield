#include "common/device/Light.h"

#include "definition.h"
#include "peripheral/WS2812B.h"
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
    mCurrentState = {false, 0, config.maxCCTMireds, false, 255, 255, 255};
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
    LOG_INF("Light changed: on=%d brightness=%u cct=%u rgb=%d (%u,%u,%u)", mCurrentState.on,
            mCurrentState.brightnessPercent, mCurrentState.cctMireds, mCurrentState.rgbMode, mCurrentState.red,
            mCurrentState.green, mCurrentState.blue);
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
        target.rgbMode = false;
        break;
    default:
        return 0;
    }

    return ApplyTarget(target.on, target.brightnessPercent, target.cctMireds, target.rgbMode, target.red, target.green,
                       target.blue);
}

int LightDevice::ApplyTarget(bool on, uint8_t brightnessPercent, uint16_t cctMireds) {
    return ApplyTarget(on, brightnessPercent, cctMireds, false, 255, 255, 255);
}

int LightDevice::ApplyTarget(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                             uint8_t green, uint8_t blue) {
    const AppConfig config = GetMemory()->Config();
    uint8_t targetBrightness = on ? ClampValue<uint8_t>(brightnessPercent, 0, 100) : 0;
    if (on && targetBrightness == 0U) {
        targetBrightness = mLastOnBrightnessPercent > 0U ? mLastOnBrightnessPercent : config.minBrightness;
    }
    if (on && targetBrightness == 0U) {
        targetBrightness = 1U;
    }

    LightState next = {on, targetBrightness,
                       ClampValue<uint16_t>(cctMireds, config.minCCTMireds, config.maxCCTMireds), rgbMode, red, green,
                       blue};

    if (!HasChanged(mCurrentState, next)) {
        return 0;
    }

    const int ret = next.rgbMode
                        ? GetWS2812B()->SetRgbTarget(next.on, next.brightnessPercent, next.red, next.green, next.blue)
                        : GetWS2812B()->SetWhiteTarget(next.on, next.brightnessPercent,
                                                       MiredsToCoolPercent(next.cctMireds));
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
