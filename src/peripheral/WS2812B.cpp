#include "peripheral/WS2812B.h"

#include "definition.h"

#include <errno.h>
#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ws2812b_peripheral, LOG_LEVEL_INF);

namespace {

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
#define WS2812B_NODE DT_ALIAS(led_strip)
const struct device* stripDevice = DEVICE_DT_GET(WS2812B_NODE);
constexpr size_t kPixelCount = DT_PROP(WS2812B_NODE, chain_length);
#endif

uint8_t ScaleChannel(uint8_t channel, uint8_t brightnessPercent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(channel) * brightnessPercent) / 100U);
}

uint8_t BlendChannel(uint8_t warm, uint8_t cool, uint8_t coolPercent, uint8_t brightnessPercent) {
    const uint16_t blended =
        (static_cast<uint16_t>(warm) * (100U - coolPercent)) + (static_cast<uint16_t>(cool) * coolPercent);
    return ScaleChannel(static_cast<uint8_t>(blended / 100U), brightnessPercent);
}

led_rgb WhitePoint(uint8_t brightnessPercent, uint8_t coolPercent) {
    constexpr uint8_t kWarmRed = 255;
    constexpr uint8_t kWarmGreen = 147;
    constexpr uint8_t kWarmBlue = 41;
    constexpr uint8_t kCoolRed = 255;
    constexpr uint8_t kCoolGreen = 209;
    constexpr uint8_t kCoolBlue = 163;

    return {
        BlendChannel(kWarmRed, kCoolRed, coolPercent, brightnessPercent),
        BlendChannel(kWarmGreen, kCoolGreen, coolPercent, brightnessPercent),
        BlendChannel(kWarmBlue, kCoolBlue, coolPercent, brightnessPercent),
    };
}

led_rgb RgbPoint(uint8_t brightnessPercent, uint8_t red, uint8_t green, uint8_t blue) {
    return {
        ScaleChannel(red, brightnessPercent),
        ScaleChannel(green, brightnessPercent),
        ScaleChannel(blue, brightnessPercent),
    };
}

} // namespace

WS2812B& WS2812B::Instance() {
    static WS2812B instance;
    return instance;
}

int WS2812B::Initialize() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
    if (!device_is_ready(stripDevice)) {
        LOG_ERR("WS2812B LED strip device not ready");
        return -ENODEV;
    }

    mState = {false, 0, 50, false, 255, 255, 255};
    const int ret = Apply();
    if (ret != 0) {
        return ret;
    }
    LOG_INF("WS2812B ready: pixels=%u driver=%s", static_cast<unsigned int>(kPixelCount), stripDevice->name);
    return 0;
#else
    LOG_WRN("WS2812B led-strip alias missing");
    return -ENODEV;
#endif
}

int WS2812B::SetPower(bool on) {
    mState.on = on;
    return Apply();
}

int WS2812B::SetBrightness(uint8_t percent) {
    mState.brightnessPercent = ClampValue<uint8_t>(percent, 0, 100);
    return Apply();
}

int WS2812B::SetWhiteTone(uint8_t coolPercent) {
    mState.coolPercent = ClampValue<uint8_t>(coolPercent, 0, 100);
    mState.rgbMode = false;
    return Apply();
}

int WS2812B::SetColor(uint8_t red, uint8_t green, uint8_t blue) {
    mState.red = red;
    mState.green = green;
    mState.blue = blue;
    mState.rgbMode = true;
    return Apply();
}

int WS2812B::SetWhiteTarget(bool on, uint8_t brightnessPercent, uint8_t coolPercent) {
    mState.on = on;
    mState.brightnessPercent = ClampValue<uint8_t>(brightnessPercent, 0, 100);
    mState.coolPercent = ClampValue<uint8_t>(coolPercent, 0, 100);
    mState.rgbMode = false;
    return Apply();
}

int WS2812B::SetRgbTarget(bool on, uint8_t brightnessPercent, uint8_t red, uint8_t green, uint8_t blue) {
    mState.on = on;
    mState.brightnessPercent = ClampValue<uint8_t>(brightnessPercent, 0, 100);
    mState.red = red;
    mState.green = green;
    mState.blue = blue;
    mState.rgbMode = true;
    return Apply();
}

int WS2812B::Apply() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
    static led_rgb pixels[kPixelCount];
    const uint8_t brightness = mState.on ? mState.brightnessPercent : 0U;
    const led_rgb pixel = mState.rgbMode ? RgbPoint(brightness, mState.red, mState.green, mState.blue)
                                         : WhitePoint(brightness, mState.coolPercent);

    for (size_t index = 0; index < kPixelCount; ++index) {
        pixels[index] = pixel;
    }

    const int ret = led_strip_update_rgb(stripDevice, pixels, kPixelCount);
    if (ret != 0) {
        LOG_ERR("WS2812B update failed: %d", ret);
        return ret;
    }
    return 0;
#else
    return -ENODEV;
#endif
}
