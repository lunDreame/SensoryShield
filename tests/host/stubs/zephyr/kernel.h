#pragma once
#include <stdint.h>
extern uint32_t testUptimeMs;
inline uint32_t k_uptime_get_32() { return testUptimeMs; }
