#include "system/memory.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(memory, LOG_LEVEL_INF);

#define APP_CONFIG_KEY "sensoryshield/app_config"
#define DEVICE_TABLE_KEY "sensoryshield/device_table"

struct StoredDeviceTable {
    uint32_t version;
    uint8_t count;
    ChildDeviceDescriptor devices[MAX_CHILD_DEVICES];
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

static AppConfig NormalizeConfig(AppConfig config) {
    config.version = CONFIG_VERSION;
    config.lightWeight = ClampValue(config.lightWeight, 0.0f, 1.0f);
    config.soundWeight = ClampValue(config.soundWeight, 0.0f, 1.0f);
    config.minBrightness = ClampValue<uint8_t>(config.minBrightness, 0, 100);
    config.maxBrightness = ClampValue<uint8_t>(config.maxBrightness, config.minBrightness, 100);
    config.minCCTMireds = ClampValue<uint16_t>(config.minCCTMireds, 1, 1000);
    config.maxCCTMireds = ClampValue<uint16_t>(config.maxCCTMireds, config.minCCTMireds, 1000);
    config.fanMaxPercent = ClampValue<uint8_t>(config.fanMaxPercent, 0, 100);
    if (config.occupancyTimeoutMs < 1000U) {
        config.occupancyTimeoutMs = 1000U;
    }
    return config;
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

    AppConfig loaded = {};
    ret = settings_load_subtree_direct(
        APP_CONFIG_KEY,
        [](const char* key, size_t len, settings_read_cb readCb, void* cbArg, void* param) {
            ARG_UNUSED(key);
            if (len != sizeof(AppConfig)) {
                return -EINVAL;
            }
            return readCb(cbArg, param, len);
        },
        &loaded);

    if (ret == 0 && loaded.version == CONFIG_VERSION) {
        mConfig = NormalizeConfig(loaded);
        LOG_INF("App config loaded");
    } else {
        LOG_INF("Default app config active");
    }

    LOG_INF("ZMS ready");
    return 0;
}

int Memory::SaveConfig(const AppConfig& config) {
    AppConfig next = NormalizeConfig(config);
    const int ret = settings_save_one(APP_CONFIG_KEY, &next, sizeof(next));
    if (ret != 0) {
        LOG_ERR("Config save failed: %d", ret);
        return ret;
    }

    mConfig = next;
    LOG_INF("App config saved");
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
            return readCb(cbArg, param, len);
        },
        &table);

    if (ret == 0 && table.version == CONFIG_VERSION && IsValidDeviceTable(table.devices, table.count)) {
        *count = table.count > maxDevices ? maxDevices : table.count;
        memcpy(devices, table.devices, sizeof(ChildDeviceDescriptor) * (*count));
    } else {
        if (ret == 0) {
            LOG_WRN("Stored device table is invalid; using defaults");
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

    mConfig = AppConfig{};
    LOG_INF("App storage reset");
    return 0;
}
