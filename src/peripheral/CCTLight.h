#pragma once

#include <stdint.h>

class CCTLight final {
  public:
    struct State {
        bool on;
        uint8_t brightnessPercent;
        uint8_t coolPercent;
    };

    static CCTLight& Instance();

    int Initialize();
    int SetPower(bool on);
    int SetBrightness(uint8_t percent);
    int SetCCTRatio(uint8_t coolPercent);
    State GetState() const {
        return mState;
    }

    CCTLight(const CCTLight&) = delete;
    CCTLight& operator=(const CCTLight&) = delete;

  private:
    CCTLight() = default;
    ~CCTLight() = default;

    int Apply();

    State mState = {};
};

inline CCTLight* GetCCTLight() {
    return &CCTLight::Instance();
}
