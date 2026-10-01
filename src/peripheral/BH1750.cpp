#include "peripheral/BH1750.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bh1750, LOG_LEVEL_INF);

#define BH1750_POWER_ON 0x01
#define BH1750_RESET 0x07
#define BH1750_CONT_HIGH_RES 0x10

#if DT_NODE_HAS_STATUS(DT_ALIAS(bh1750), okay)
static const struct i2c_dt_spec bh1750Spec = I2C_DT_SPEC_GET(DT_ALIAS(bh1750));
#endif

BH1750& BH1750::Instance() {
    static BH1750 instance;
    return instance;
}

int BH1750::Initialize() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(bh1750), okay)
    if (!device_is_ready(bh1750Spec.bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    uint8_t command = BH1750_POWER_ON;
    int ret = i2c_write_dt(&bh1750Spec, &command, sizeof(command));
    if (ret != 0) {
        LOG_ERR("Power on failed: %d", ret);
        return ret;
    }

    command = BH1750_RESET;
    ret = i2c_write_dt(&bh1750Spec, &command, sizeof(command));
    if (ret != 0) {
        LOG_WRN("Reset failed: %d", ret);
    }

    command = BH1750_CONT_HIGH_RES;
    ret = i2c_write_dt(&bh1750Spec, &command, sizeof(command));
    if (ret != 0) {
        LOG_ERR("Mode set failed: %d", ret);
        return ret;
    }

    mHealthy = true;
    LOG_INF("BH1750 ready");
    return 0;
#else
    LOG_WRN("BH1750 devicetree alias missing");
    mHealthy = false;
    return -ENODEV;
#endif
}

int BH1750::ReadLux(float* lux) {
    if (lux == nullptr) {
        return -EINVAL;
    }

#if DT_NODE_HAS_STATUS(DT_ALIAS(bh1750), okay)
    uint8_t raw[2] = {};
    const int ret = i2c_read_dt(&bh1750Spec, raw, sizeof(raw));
    if (ret != 0) {
        ++mErrorCount;
        mHealthy = false;
        LOG_WRN("Read retry: %u", mErrorCount);
        return ret;
    }

    const uint16_t level = (static_cast<uint16_t>(raw[0]) << 8U) | raw[1];
    *lux = static_cast<float>(level) / 1.2f;
    mErrorCount = 0;
    mHealthy = true;
    return 0;
#else
    return -ENODEV;
#endif
}
