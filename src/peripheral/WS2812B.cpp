#include "peripheral/WS2812B.h"

#include "definition.h"

#include <errno.h>
#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util_macro.h>
#include <hal/nrf_gpio.h>

LOG_MODULE_REGISTER(ws2812b_peripheral, LOG_LEVEL_INF);

namespace {

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
#define WS2812B_NODE DT_ALIAS(led_strip)
const struct gpio_dt_spec dataGpio = GPIO_DT_SPEC_GET(WS2812B_NODE, gpios);
NRF_GPIO_Type* const dataPort = reinterpret_cast<NRF_GPIO_Type*>(DT_REG_ADDR(DT_GPIO_CTLR(WS2812B_NODE, gpios)));
constexpr uint32_t kDataPinMask = BIT(DT_GPIO_PIN(WS2812B_NODE, gpios));
constexpr size_t kPixelCount = DT_PROP(DT_ALIAS(led_strip), chain_length);
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
constexpr uint16_t kResetDelayUs = DT_PROP(WS2812B_NODE, reset_delay);
#endif

struct WsPixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

#define WS2812B_NOPS(i, _) "nop\n"
#define WS2812B_NOP_N_TIMES(n) LISTIFY(n, WS2812B_NOPS, ())

#define WS2812B_SET_HIGH "str %[p], [%[r], #0]\n"
#define WS2812B_SET_LOW "str %[p], [%[r], #4]\n"

#define WS2812B_ONE_BIT(base, pin)                                                                             \
    do {                                                                                                       \
        __asm volatile(WS2812B_SET_HIGH WS2812B_NOP_N_TIMES(89) WS2812B_SET_LOW WS2812B_NOP_N_TIMES(76) ::    \
                           [r] "l"(base), [p] "l"(pin));                                                     \
    } while (false)

#define WS2812B_ZERO_BIT(base, pin)                                                                            \
    do {                                                                                                       \
        __asm volatile(WS2812B_SET_HIGH WS2812B_NOP_N_TIMES(44) WS2812B_SET_LOW WS2812B_NOP_N_TIMES(102) ::   \
                           [r] "l"(base), [p] "l"(pin));                                                     \
    } while (false)

uint8_t ScaleChannel(uint8_t channel, uint8_t brightnessPercent) {
    return static_cast<uint8_t>((static_cast<uint16_t>(channel) * brightnessPercent) / 100U);
}

uint8_t BlendChannel(uint8_t warm, uint8_t cool, uint8_t coolPercent, uint8_t brightnessPercent) {
    const uint16_t blended =
        (static_cast<uint16_t>(warm) * (100U - coolPercent)) + (static_cast<uint16_t>(cool) * coolPercent);
    return ScaleChannel(static_cast<uint8_t>(blended / 100U), brightnessPercent);
}

WsPixel WhitePoint(uint8_t brightnessPercent, uint8_t coolPercent) {
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

WsPixel RgbPoint(uint8_t brightnessPercent, uint8_t red, uint8_t green, uint8_t blue) {
    return {
        ScaleChannel(red, brightnessPercent),
        ScaleChannel(green, brightnessPercent),
        ScaleChannel(blue, brightnessPercent),
    };
}

#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
void SendByte(uint8_t value) {
    volatile uint32_t* outset = &dataPort->OUTSET;

    for (int bit = 7; bit >= 0; --bit) {
        if ((value & BIT(bit)) != 0U) {
            WS2812B_ONE_BIT(outset, kDataPinMask);
        } else {
            WS2812B_ZERO_BIT(outset, kDataPinMask);
        }
    }
}

void SendPixel(const WsPixel& pixel) {
    SendByte(pixel.g);
    SendByte(pixel.r);
    SendByte(pixel.b);
}

int SendPixels(const WsPixel* pixels, size_t pixelCount) {
    const unsigned int key = irq_lock();

    for (size_t index = 0; index < pixelCount; ++index) {
        SendPixel(pixels[index]);
    }

    irq_unlock(key);
    k_busy_wait(kResetDelayUs);
    return 0;
}
#endif

} // namespace

WS2812B& WS2812B::Instance() {
    static WS2812B instance;
    return instance;
}

int WS2812B::Initialize() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(led_strip), okay)
    if (!gpio_is_ready_dt(&dataGpio)) {
        LOG_ERR("WS2812B data GPIO not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&dataGpio, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        LOG_ERR("WS2812B data GPIO configure failed: %d", ret);
        return ret;
    }

    mState = {false, 0, 50, false, 255, 255, 255};
    ret = Apply();
    if (ret != 0) {
        return ret;
    }
    LOG_INF("WS2812B ready: pixels=%u gpio=P%u.%u", static_cast<unsigned int>(kPixelCount),
            static_cast<unsigned int>(DT_PROP(DT_GPIO_CTLR(WS2812B_NODE, gpios), port)),
            static_cast<unsigned int>(DT_GPIO_PIN(WS2812B_NODE, gpios)));
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
    static WsPixel pixels[kPixelCount];
    const uint8_t brightness = mState.on ? mState.brightnessPercent : 0U;
    const WsPixel pixel = mState.rgbMode ? RgbPoint(brightness, mState.red, mState.green, mState.blue)
                                         : WhitePoint(brightness, mState.coolPercent);

    for (size_t index = 0; index < kPixelCount; ++index) {
        pixels[index] = pixel;
    }

    const int ret = SendPixels(pixels, kPixelCount);
    if (ret != 0) {
        LOG_ERR("WS2812B update failed: %d", ret);
        return ret;
    }
    return 0;
#else
    return -ENODEV;
#endif
}
