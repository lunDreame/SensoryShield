#pragma once

#include <stdint.h>

class WS2812B final {
  public:
    struct State {
        bool on;
        uint8_t brightnessPercent;
        uint8_t coolPercent;
        bool rgbMode;
        uint8_t red;
        uint8_t green;
        uint8_t blue;
    };

    static WS2812B& Instance();

    int Initialize();
    int SetPower(bool on);
    int SetBrightness(uint8_t percent);
    int SetWhiteTone(uint8_t coolPercent);
    int SetColor(uint8_t red, uint8_t green, uint8_t blue);
    int SetWhiteTarget(bool on, uint8_t brightnessPercent, uint8_t coolPercent);
    int SetRgbTarget(bool on, uint8_t brightnessPercent, uint8_t red, uint8_t green, uint8_t blue);
    State GetState() const {
        return mState;
    }

    WS2812B(const WS2812B&) = delete;
    WS2812B& operator=(const WS2812B&) = delete;

  private:
    WS2812B() = default;
    ~WS2812B() = default;

    int Apply();

    State mState = {};
};

inline WS2812B* GetWS2812B() {
    return &WS2812B::Instance();
}
