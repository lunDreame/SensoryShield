#include <zephyr/logging/log.h>

#include "system/system.h"

LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

int main() {
    const int ret = GetSystem()->Initialize();
    if (ret != 0) {
        LOG_ERR("System init failed: %d", ret);
        return ret;
    }

    LOG_INF("System ready");
    return 0;
}
