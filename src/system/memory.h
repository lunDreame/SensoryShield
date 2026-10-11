#pragma once

#include "definition.h"
#include "common/algorithm/environment_measurement.h"

class Memory final {
  public:
    static Memory& Instance();

    int Initialize();
    const AppConfig& Config() const {
        return mConfig;
    }
    int SaveConfig(const AppConfig& config);
    int SaveSetup(const AppConfig& config, const InitialEnvironment& initial);
    const InitialEnvironment& InitialBaseline() const { return mInitial; }
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
    InitialEnvironment mInitial = {};
};

inline Memory* GetMemory() {
    return &Memory::Instance();
}
