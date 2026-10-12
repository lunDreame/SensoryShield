#pragma once
#include "definition.h"

// Asynchronous end-of-boost failure can leave the logical target unchanged.
// Retry failed physical output at most once per algorithm interval, including wrap.
inline bool OutputRetryDue(bool outputValid, uint32_t now, uint32_t lastAttemptMs) {
    return !outputValid && ElapsedUptimeMs(now, lastAttemptMs) >= ALGORITHM_PERIOD_MS;
}
