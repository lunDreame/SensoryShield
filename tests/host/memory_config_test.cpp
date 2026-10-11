#include "system/memory.h"
#include <zephyr/settings/settings.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <map>
#include <string>
#include <vector>
#include <cstdio>

static std::map<std::string, std::vector<unsigned char>> storage;
static bool failSave = false;
int settings_subsys_init() { return 0; }
static ssize_t Read(void* source, void* target, size_t length) {
    auto& bytes = *static_cast<std::vector<unsigned char>*>(source);
    memcpy(target, bytes.data(), length);
    return length;
}
int settings_load_subtree_direct(const char* key, settings_load_direct_cb cb, void* param) {
    auto found = storage.find(key);
    if (found == storage.end()) return 0;
    const int ret = cb(nullptr, found->second.size(), Read, &found->second, param);
    return ret < 0 ? ret : 0;
}
int settings_save_one(const char* key, const void* data, size_t length) {
    if (failSave) return -EIO;
    const auto* bytes = static_cast<const unsigned char*>(data);
    storage[key] = {bytes, bytes + length};
    return 0;
}
int settings_delete(const char* key) { return storage.erase(key) ? 0 : -ENOENT; }
struct LegacyConfig {
    uint32_t version; float lightWeight, soundWeight;
    uint8_t minBrightness, maxBrightness;
    uint16_t minCCTMireds, maxCCTMireds;
    uint8_t fanMaxPercent; uint32_t occupancyTimeoutMs; bool profileConfigured;
};
static_assert(sizeof(LegacyConfig) == 28, "v4 config record layout changed");

int main() {
    auto& memory = Memory::Instance();
    memory.Initialize();
    AppConfig config;
    config.preferredCCTMireds = 250;
    config.preferredBrightness = 60;
    config.preferredFanPercent = 40;
    config.lightWeight = 0.8f;
    config.profileConfigured = true;
    assert(memory.SaveConfig(config) == 0);
    memory.Initialize();
    assert(memory.Config().preferredCCTMireds == 250);
    assert(memory.Config().preferredBrightness == 60);
    assert(memory.Config().preferredFanPercent == 40);
    assert(memory.Config().profileConfigured);
    assert(memory.Config().lightWeight == 0.8f);
    config.minCCTMireds = 300;
    config.maxCCTMireds = 400;
    assert(memory.SaveConfig(config) == 0);
    memory.Initialize();
    assert(memory.Config().preferredCCTMireds == 300);
    // Old padding is deliberately non-zero: it is not a preferred colour.
    LegacyConfig legacy;
    memset(&legacy, 0xff, sizeof(legacy));
    legacy.version = LEGACY_CONFIG_VERSION;
    legacy.lightWeight = .8f; legacy.soundWeight = .75f;
    legacy.minBrightness = 15; legacy.maxBrightness = 90;
    legacy.minCCTMireds = 250; legacy.maxCCTMireds = 370;
    legacy.fanMaxPercent = 80; legacy.occupancyTimeoutMs = 60000;
    legacy.profileConfigured = true;
    settings_save_one("sensoryshield/app_config", &legacy, sizeof(legacy));
    memory.Initialize();
    assert(memory.Config().version == CONFIG_VERSION);
    assert(memory.Config().preferredBrightness == 90);
    assert(memory.Config().preferredCCTMireds == 310);
    assert(memory.Config().lightWeight == .8f);
    assert(memory.Config().fanMaxPercent == 80);
    assert(memory.Config().profileConfigured);
    assert(memory.Config().occupancyTimeoutMs == 60000);
    ChildDeviceDescriptor device = {1, 1, true, 3, "light"};
    memory.SaveDeviceTable(&device, 1);
    uint32_t oldVersion = LEGACY_CONFIG_VERSION;
    memcpy(storage["sensoryshield/device_table"].data(), &oldVersion, sizeof(oldVersion));
    uint8_t count = 0; ChildDeviceDescriptor restored;
    assert(memory.LoadDeviceTable(&restored, 1, &count) == 0 && count == 1);
    ControlTarget manual{}; manual.lightOn = true; manual.brightnessPercent = 51; manual.cctMireds = 333;
    memory.SaveRuntimeState(ControlMode::Manual, manual);
    memcpy(storage["sensoryshield/runtime_state"].data(), &oldVersion, sizeof(oldVersion));
    ControlMode mode = ControlMode::Auto; ControlTarget target{};
    assert(memory.LoadRuntimeState(&mode, &target) == 0);
    assert(mode == ControlMode::Manual && target.brightnessPercent == 51 && target.cctMireds == 333);
    InitialEnvironment initial;
    initial.count = ENVIRONMENT_SAMPLE_CAPACITY;
    for (uint32_t i = 0; i < initial.count; ++i) { initial.lux[i] = 100 + i; initial.sound[i] = .01f + .0001f * i; }
    assert(memory.SaveSetup(memory.Config(), initial) == 0);
    memory.Initialize();
    assert(memory.InitialBaseline().Valid() && memory.InitialBaseline().lux[30] == 130);
    auto updated = memory.Config(); updated.preferredBrightness = 60;
    assert(memory.SaveConfig(updated) == 0);
    memory.Initialize();
    assert(memory.Config().preferredBrightness == 60 && memory.InitialBaseline().Valid());
    failSave = true;
    auto rejected = updated; rejected.preferredBrightness = 70;
    assert(memory.SaveSetup(rejected, initial) == -EIO);
    assert(memory.Config().preferredBrightness == 60 && memory.InitialBaseline().lux[30] == 130);
    failSave = false;
    memory.Initialize();
    assert(memory.Config().preferredBrightness == 60 && memory.InitialBaseline().lux[30] == 130);
    initial.sound[3] = 2;
    assert(memory.SaveSetup(updated, initial) == -EINVAL);
    assert(memory.InitialBaseline().sound[3] < 1);
    // v6 used the same record size, but its byte 29 was padding, not a fan preference.
    struct SetupRecord { AppConfig config; InitialEnvironment initial; } oldSetup{memory.Config(), memory.InitialBaseline()};
    static_assert(sizeof(AppConfig) == 32, "Preserve v6 storage offsets");
    static_assert(offsetof(AppConfig, preferredFanPercent) == 29, "v6 padding migration offset changed");
    oldSetup.config.version = SETUP_CONFIG_VERSION;
    oldSetup.config.preferredFanPercent = 255;
    settings_save_one("sensoryshield/app_config", &oldSetup, sizeof(oldSetup));
    memory.Initialize();
    assert(memory.Config().preferredFanPercent == 60 && memory.InitialBaseline().Valid());
    assert(memory.Config().preferredBrightness == 60);
    // A v5 record preserves tone and inherits its previous automatic return brightness.
    auto previous = memory.Config(); previous.version = PREVIOUS_CONFIG_VERSION;
    settings_save_one("sensoryshield/app_config", &previous, 28);
    memory.Initialize();
    assert(memory.Config().preferredCCTMireds == previous.preferredCCTMireds);
    assert(memory.Config().preferredBrightness == previous.maxBrightness);
    assert(!memory.InitialBaseline().Valid());
    assert(memory.FactoryReset() == 0);
    assert(!memory.InitialBaseline().Valid());
    assert(memory.Config().preferredCCTMireds == 333);
    puts("preferred tone persistence, v4 migration and existing device/runtime records passed");
}
