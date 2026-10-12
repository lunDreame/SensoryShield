#pragma once
#define LOG_LEVEL_INF 0
#define LOG_MODULE_REGISTER(...) static_assert(true, "host log module")
template<class... Args> inline void host_log(Args&&...) {}
#define LOG_INF(...) host_log(__VA_ARGS__)
#define LOG_WRN(...) host_log(__VA_ARGS__)
#define LOG_ERR(...) host_log(__VA_ARGS__)
#define LOG_DBG(...) host_log(__VA_ARGS__)
