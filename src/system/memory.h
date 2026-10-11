#pragma once

#include "definition.h"

class Memory final {
  public:
    static Memory& Instance();

    int Initialize();
    const AppConfig& Config() const {
        return mConfig;
    }
    int SaveConfig(const AppConfig& config);
    int LoadEnvironmentBaseline(EnvironmentBaseline* baseline);
    int SaveEnvironmentBaseline(const EnvironmentBaseline& baseline);
    int LoadRuntimeState(ControlMode* mode, ControlTarget* target);
    int SaveRuntimeState(ControlMode mode, const ControlTarget& target);
    int LoadDeviceTable(ChildDeviceDescriptor* devices, uint8_t maxDevices, uint8_t* count);
    int SaveDeviceTable(const ChildDeviceDescriptor* devices, uint8_t count);
    int FactoryReset();

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

  private:
    Memory() = default;
    ~Memory() = default;

    AppConfig mConfig = {};
};

inline Memory* GetMemory() {
    return &Memory::Instance();
}
