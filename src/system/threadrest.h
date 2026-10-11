#pragma once

#include "definition.h"

#include <stddef.h>

class ThreadRest final {
  public:
    static ThreadRest& Instance();

    int Initialize();
    int HandleLightCommand(bool on, uint8_t brightnessPercent, uint16_t cctMireds);
    int HandleLightCommand(bool on, uint8_t brightnessPercent, uint16_t cctMireds, bool rgbMode, uint8_t red,
                           uint8_t green, uint8_t blue);
    int HandleFanCommand(bool on, uint8_t speedPercent);
    int HandleModeCommand(ControlMode mode, uint32_t overrideDurationMinutes = 15U);
    int HandleProfileUpdate(const AppConfig& config);
    int HandleFactoryReset();
    int BuildStatusJson(char* buffer, size_t bufferSize) const;
    int BuildBaselineJson(char* buffer, size_t bufferSize) const;
    int BuildProfileJson(char* buffer, size_t bufferSize) const;
    int BuildDiagnosticsJson(char* buffer, size_t bufferSize) const;

    ThreadRest(const ThreadRest&) = delete;
    ThreadRest& operator=(const ThreadRest&) = delete;

  private:
    ThreadRest() = default;
    ~ThreadRest() = default;
};

inline ThreadRest* GetThreadRest() {
    return &ThreadRest::Instance();
}
