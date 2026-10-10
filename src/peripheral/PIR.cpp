#include "peripheral/PIR.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pir, LOG_LEVEL_INF);

#if DT_NODE_HAS_STATUS(DT_ALIAS(pir), okay)
static const struct gpio_dt_spec pirSpec = GPIO_DT_SPEC_GET(DT_ALIAS(pir), gpios);
#endif

PIR& PIR::Instance() {
    static PIR instance;
    return instance;
}

int PIR::Initialize() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(pir), okay)
    if (!gpio_is_ready_dt(&pirSpec)) {
        LOG_ERR("GPIO not ready");
        return -ENODEV;
    }

    const int ret = gpio_pin_configure_dt(&pirSpec, GPIO_INPUT);
    if (ret != 0) {
        LOG_ERR("GPIO configure failed: %d", ret);
        return ret;
    }

    mHealthy = true;
    Poll();
    LOG_INF("PIR ready");
    return 0;
#else
    LOG_WRN("PIR devicetree alias missing");
    mHealthy = false;
    return -ENODEV;
#endif
}

void PIR::Poll() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(pir), okay)
    const int value = gpio_pin_get_dt(&pirSpec);
    if (value < 0) {
        mHealthy = false;
        LOG_WRN("GPIO read failed: %d", value);
        return;
    }

    const uint32_t now = k_uptime_get_32();
    if (value > 0) {
        if (!mOccupied) {
            LOG_INF("Occupancy changed: occupied");
        }
        mOccupied = true;
        mLastMotionMs = now;
        mHealthy = true;
        return;
    }

    if (mOccupied) {
        mOccupied = false;
        LOG_INF("Occupancy changed: clear");
    }
    mHealthy = true;
#endif
}
