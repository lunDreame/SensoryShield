#pragma once
#define LOG_LEVEL_INF 0
#define LOG_MODULE_REGISTER(...) static_assert(true, "host log module")
#define LOG_INF(...) do { } while (false)
#define LOG_WRN(...) do { } while (false)
#define LOG_ERR(...) do { } while (false)
