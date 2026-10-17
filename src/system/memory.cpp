#include "system/memory.h"

#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(memory, LOG_LEVEL_INF);

#define APP_CONFIG_KEY "sensoryshield/app_config"
#define RUNTIME_STATE_KEY "sensoryshield/runtime_state"
#define DEVICE_TABLE_KEY "sensoryshield/device_table"

struct StoredSetup { AppConfig config; InitialEnvironment initial; };
struct StoredSetupV8 { unsigned char config[44]; InitialEnvironment initial; };
static_assert(offsetof(AppConfig, occupancyAutomationEnabled) == 44, "Preserve v8 config offsets");
struct StoredSetupV7 { unsigned char config[32]; InitialEnvironment initial; };
static_assert(offsetof(AppConfig, normalCCTMireds) == 32, "Preserve old config offsets");

struct StoredDeviceTable {
    uint32_t version;
    uint8_t count;
    ChildDeviceDescriptor devices[MAX_CHILD_DEVICES];
};

struct StoredRuntimeState {
    uint32_t version;
    uint8_t mode;
    ControlTarget target;
};

static bool IsValidDeviceTable(const ChildDeviceDescriptor* devices, uint8_t count) {
    if (devices == nullptr || count > MAX_CHILD_DEVICES) {
        return false;
    }

    for (uint8_t index = 0; index < count; ++index) {
        if (devices[index].logicalId == 0U || devices[index].preferredEndpointId == 0U) {
            return false;
        }
        for (uint8_t previous = 0; previous < index; ++previous) {
            if (devices[index].logicalId == devices[previous].logicalId ||
                devices[index].preferredEndpointId == devices[previous].preferredEndpointId) {
                return false;
            }
        }
    }
    return true;
}

static bool IsValidMode(ControlMode mode) {
    return mode == ControlMode::Auto || mode == ControlMode::Manual;
}

static AppConfig NormalizeConfig(AppConfig config) {
    config.version = CONFIG_VERSION;
    config.lightWeight = ClampValue(config.lightWeight, 0.0f, 1.0f);
    config.soundWeight = ClampValue(config.soundWeight, 0.0f, 1.0f);
    config.minBrightness = ClampValue<uint8_t>(config.minBrightness, 0, 100);
    config.maxBrightness = ClampValue<uint8_t>(config.maxBrightness, config.minBrightness, 100);
    config.preferredBrightness = ClampValue<uint8_t>(config.preferredBrightness, config.minBrightness, config.maxBrightness);
    config.minCCTMireds = ClampValue<uint16_t>(config.minCCTMireds, 1, 1000);
    config.maxCCTMireds = ClampValue<uint16_t>(config.maxCCTMireds, config.minCCTMireds, 1000);
    config.preferredCCTMireds = ClampValue<uint16_t>(config.preferredCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    config.normalCCTMireds = ClampValue<uint16_t>(config.normalCCTMireds, config.minCCTMireds, config.maxCCTMireds);
    config.reliefFanPercent = ClampValue<uint8_t>(config.reliefFanPercent, 0, 100);
    if (config.reliefFanPercent > 0 && config.reliefFanPercent < FAN_MIN_DUTY_PERCENT) config.reliefFanPercent = FAN_MIN_DUTY_PERCENT;
    config.reliefHoldMs = ClampValue<uint32_t>(config.reliefHoldMs, 0, MAX_RELIEF_HOLD_MS);
    config.recoverConfirmMs = ClampValue<uint32_t>(config.recoverConfirmMs, 250, 60000);
    config.fanMaxPercent = ClampValue<uint8_t>(config.fanMaxPercent, 0, 100);
    config.preferredFanPercent = NormalizeFanPercent(config.preferredFanPercent, config.fanMaxPercent);
    if (config.occupancyTimeoutMs < 1000U) {
        config.occupancyTimeoutMs = AUTO_OCCUPANCY_HOLD_MS;
    }
    config.occupancyTimeoutMs = ClampValue<uint32_t>(config.occupancyTimeoutMs, 1000U, MAX_OCCUPANCY_HOLD_MS);
    return config;
}

static ControlTarget NormalizeTarget(ControlTarget target, const AppConfig& config) {
    target.brightnessPercent = ClampValue<uint8_t>(target.brightnessPercent, 0, 100);
    target.cctMireds = ClampValue<uint16_t>(target.cctMireds, config.minCCTMireds, config.maxCCTMireds);
    target.fanPercent = NormalizeFanPercent(target.fanPercent, config.fanMaxPercent);
    if (!target.lightOn) {
        target.brightnessPercent = 0;
    } else if (target.brightnessPercent == 0U) {
        target.brightnessPercent = config.minBrightness > 0U ? config.minBrightness : 1U;
    }
    if (!target.fanOn) {
        target.fanPercent = 0;
    }
    target.sensoryScore = 0.0f;
    return target;
}

Memory& Memory::Instance() {
    static Memory instance;
    return instance;
}

int Memory::Initialize() {
    mConfig = AppConfig{};

    int ret = settings_subsys_init();
    if (ret != 0 && ret != -EALREADY) {
        LOG_ERR("Settings init failed: %d", ret);
        return ret;
    }

    struct LoadContext { StoredSetup setup{}; bool read = false; } loaded;
    ret = settings_load_subtree_direct(
        APP_CONFIG_KEY,
        [](const char* key, size_t len, settings_read_cb readCb, void* cbArg, void* param) -> int {
            ARG_UNUSED(key);
            auto* context = static_cast<LoadContext*>(param);
            if (len == sizeof(StoredSetup)) {
                const auto bytes = readCb(cbArg, &context->setup, len);
                if (bytes != static_cast<ssize_t>(len) ||
                    (context->setup.config.version != CONFIG_VERSION &&
                     context->setup.config.version != OCCUPANCY_CONFIG_VERSION)) return -EINVAL;
            } else if (len == sizeof(StoredSetupV8)) {
                StoredSetupV8 old{};
                if (readCb(cbArg, &old, len) != static_cast<ssize_t>(len)) return -EINVAL;
                memcpy(reinterpret_cast<unsigned char*>(&context->setup.config), old.config, sizeof(old.config));
                if (context->setup.config.version != RELIEF_CONFIG_VERSION) return -EINVAL;
                context->setup.initial = old.initial;
            } else if (len == sizeof(StoredSetupV7)) {
                StoredSetupV7 old{};
                if (readCb(cbArg, &old, len) != static_cast<ssize_t>(len)) return -EINVAL;
                memcpy(reinterpret_cast<unsigned char*>(&context->setup.config), old.config, sizeof(old.config));
                if (context->setup.config.version != OUTPUT_CONFIG_VERSION && context->setup.config.version != SETUP_CONFIG_VERSION) return -EINVAL;
                context->setup.initial = old.initial;
                context->setup.config.normalCCTMireds = context->setup.config.minCCTMireds;
            } else if (len == 28U) {
                if (readCb(cbArg, &context->setup.config, len) != static_cast<ssize_t>(len)) return -EINVAL;
                if (context->setup.config.version != LEGACY_CONFIG_VERSION && context->setup.config.version != PREVIOUS_CONFIG_VERSION) return -EINVAL;
                context->setup.config.normalCCTMireds = context->setup.config.minCCTMireds;
            } else return -EINVAL;
            if (context->setup.config.version < OCCUPANCY_CONFIG_VERSION)
                context->setup.config.occupancyAutomationEnabled = true;
#if DEMO_DEFAULTS
            // Migrate the former demo default once; preserve custom outputs,
            // sensitivity, tone, timings, occupancy and Matter registrations.
            if (context->setup.config.version < CONFIG_VERSION && context->setup.config.reliefFanPercent == 20)
                context->setup.config.reliefFanPercent = 0;
#endif
            context->read = true;
            return 0;
        }, &loaded);
    auto& config = loaded.setup.config;
    if (ret == 0 && loaded.read && (config.version == LEGACY_CONFIG_VERSION || config.version == PREVIOUS_CONFIG_VERSION)) {
        if (config.version == LEGACY_CONFIG_VERSION) {
            config.preferredCCTMireds = config.minCCTMireds <= config.maxCCTMireds
                ? config.minCCTMireds + (config.maxCCTMireds - config.minCCTMireds) / 2U : config.minCCTMireds;
        }
        config.preferredBrightness = config.maxBrightness;
        config.version = CONFIG_VERSION;
    }
    if (ret == 0 && loaded.read && config.version == SETUP_CONFIG_VERSION) {
        config.preferredFanPercent = AUTO_FAN_NORMAL_PERCENT;
        config.version = CONFIG_VERSION;
    }
    if (config.version == OUTPUT_CONFIG_VERSION || config.version == RELIEF_CONFIG_VERSION ||
        config.version == OCCUPANCY_CONFIG_VERSION) config.version = CONFIG_VERSION;
    mInitial = {};
    if (ret == 0 && loaded.read && config.version == CONFIG_VERSION) {
        mConfig = NormalizeConfig(config);
        if (loaded.setup.initial.Valid()) mInitial = loaded.setup.initial;
        LOG_INF("App config loaded");
    } else LOG_INF("Default app config active");

    LOG_INF("ZMS ready");
    return 0;
}

int Memory::SaveConfig(const AppConfig& config) {
    return SaveSetup(config, mInitial);
}

int Memory::SaveSetup(const AppConfig& config, const InitialEnvironment& initial) {
    if (initial.count != 0 && !initial.Valid()) return -EINVAL;
    StoredSetup next = {NormalizeConfig(config), initial};
    // Preference and observations commit atomically in one settings record.
    const int ret = settings_save_one(APP_CONFIG_KEY, &next, sizeof(next));
    if (ret != 0) { LOG_ERR("Config save failed: %d", ret); return ret; }
    mConfig = next.config; mInitial = next.initial;
    return 0;
}

int Memory::LoadRuntimeState(ControlMode* mode, ControlTarget* target) {
    if (mode == nullptr || target == nullptr) {
        return -EINVAL;
    }

    StoredRuntimeState state = {};
    const int ret = settings_load_subtree_direct(
        RUNTIME_STATE_KEY,
        [](const char* key, size_t len, settings_read_cb readCb, void* cbArg, void* param) {
            ARG_UNUSED(key);
            if (len != sizeof(StoredRuntimeState)) {
                return -EINVAL;
            }
            return static_cast<int>(readCb(cbArg, param, len));
        },
        &state);

    const ControlMode storedMode = static_cast<ControlMode>(state.mode);
    if (ret != 0 || (state.version != CONFIG_VERSION && state.version != OCCUPANCY_CONFIG_VERSION && state.version != LEGACY_CONFIG_VERSION && state.version != PREVIOUS_CONFIG_VERSION && state.version != SETUP_CONFIG_VERSION && state.version != OUTPUT_CONFIG_VERSION && state.version != RELIEF_CONFIG_VERSION) ||
        !IsValidMode(storedMode)) {
        if (ret == 0) {
            LOG_WRN("Stored runtime state is invalid, clearing");
            (void)settings_delete(RUNTIME_STATE_KEY);
        }
        return ret;
    }

    *mode = storedMode;
    *target = NormalizeTarget(state.target, mConfig);
    LOG_INF("Runtime state loaded: mode=%u", static_cast<unsigned int>(*mode));
    return 0;
}

int Memory::SaveRuntimeState(ControlMode mode, const ControlTarget& target) {
    if (mode == ControlMode::Override) {
        mode = ControlMode::Manual;
    }
    if (mode == ControlMode::Safe) {
        return 0;
    }
    if (!IsValidMode(mode)) {
        return -EINVAL;
    }

    StoredRuntimeState state = {};
    state.version = CONFIG_VERSION;
    state.mode = static_cast<uint8_t>(mode);
    state.target = NormalizeTarget(target, mConfig);

    const int ret = settings_save_one(RUNTIME_STATE_KEY, &state, sizeof(state));
    if (ret != 0) {
        LOG_ERR("Runtime state save failed: %d", ret);
        return ret;
    }
    return 0;
}

int Memory::LoadDeviceTable(ChildDeviceDescriptor* devices, uint8_t maxDevices, uint8_t* count) {
    if (devices == nullptr || count == nullptr || maxDevices == 0U) {
        return -EINVAL;
    }

    StoredDeviceTable table = {};
    const int ret = settings_load_subtree_direct(
        DEVICE_TABLE_KEY,
        [](const char* key, size_t len, settings_read_cb readCb, void* cbArg, void* param) {
            ARG_UNUSED(key);
            if (len != sizeof(StoredDeviceTable)) {
                return -EINVAL;
            }
            return static_cast<int>(readCb(cbArg, param, len));
        },
        &table);

    if (ret == 0 && (table.version == CONFIG_VERSION || table.version == OCCUPANCY_CONFIG_VERSION || table.version == LEGACY_CONFIG_VERSION || table.version == PREVIOUS_CONFIG_VERSION || table.version == SETUP_CONFIG_VERSION || table.version == OUTPUT_CONFIG_VERSION || table.version == RELIEF_CONFIG_VERSION) &&
        IsValidDeviceTable(table.devices, table.count)) {
        *count = table.count > maxDevices ? maxDevices : table.count;
        memcpy(devices, table.devices, sizeof(ChildDeviceDescriptor) * (*count));
    } else {
        if (ret == 0) {
            LOG_WRN("Stored device table is invalid, using defaults");
            (void)settings_delete(DEVICE_TABLE_KEY);
        }
        *count = 0;
    }

    return ret == -ENOENT ? 0 : ret;
}

int Memory::SaveDeviceTable(const ChildDeviceDescriptor* devices, uint8_t count) {
    if (!IsValidDeviceTable(devices, count)) {
        return -EINVAL;
    }

    StoredDeviceTable table = {};
    table.version = CONFIG_VERSION;
    table.count = count;
    memcpy(table.devices, devices, sizeof(ChildDeviceDescriptor) * count);

    const int ret = settings_save_one(DEVICE_TABLE_KEY, &table, sizeof(table));
    if (ret != 0) {
        LOG_ERR("Device table save failed: %d", ret);
    }
    return ret;
}

int Memory::FactoryReset() {
    int ret = settings_delete(APP_CONFIG_KEY);
    if (ret != 0 && ret != -ENOENT) {
        LOG_ERR("Config delete failed: %d", ret);
        return ret;
    }

    ret = settings_delete(DEVICE_TABLE_KEY);
    if (ret != 0 && ret != -ENOENT) {
        LOG_ERR("Device table delete failed: %d", ret);
        return ret;
    }

    ret = settings_delete(RUNTIME_STATE_KEY);
    if (ret != 0 && ret != -ENOENT) {
        LOG_ERR("Runtime state delete failed: %d", ret);
        return ret;
    }

    mConfig = AppConfig{};
    mInitial = {};
    LOG_INF("App storage reset");
    return 0;
}
