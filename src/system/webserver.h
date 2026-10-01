#pragma once

#include "definition.h"

#include <stddef.h>

class WebServer final {
  public:
    static WebServer& Instance();

    int Initialize();
    int HandleLightCommand(bool on, uint8_t brightnessPercent, uint16_t cctMireds);
    int HandleFanCommand(bool on, uint8_t speedPercent);
    int HandleModeCommand(ControlMode mode);
    int HandleProfileUpdate(const AppConfig& config);
    int HandleFactoryReset();
    int BuildStatusJson(char* buffer, size_t bufferSize) const;
    int BuildProfileJson(char* buffer, size_t bufferSize) const;
    int BuildDiagnosticsJson(char* buffer, size_t bufferSize) const;

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

  private:
    WebServer() = default;
    ~WebServer() = default;
};

inline WebServer* GetWebServer() {
    return &WebServer::Instance();
}
