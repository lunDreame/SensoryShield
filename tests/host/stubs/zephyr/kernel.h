#pragma once
#include <stddef.h>
#include <stdint.h>
#include <mutex>
extern uint32_t testUptimeMs;
inline uint32_t k_uptime_get_32() { return testUptimeMs; }

#define K_FOREVER 0
#define K_MSEC(ms) (ms)
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) ((type*)((char*)(ptr) - offsetof(type, member)))
#endif

struct k_mutex { std::mutex value; };
inline void k_mutex_init(k_mutex*) {}
inline int k_mutex_lock(k_mutex* mutex, int) { mutex->value.lock(); return 0; }
inline int k_mutex_unlock(k_mutex* mutex) { mutex->value.unlock(); return 0; }

struct k_work { void (*handler)(k_work*) = nullptr; };
struct k_work_delayable { k_work work; bool pending = false; uint32_t delayMs = 0; };
struct k_work_sync {};
inline k_work_delayable* hostLastScheduledWork = nullptr;
inline k_work_delayable* k_work_delayable_from_work(k_work* work) {
    return CONTAINER_OF(work, k_work_delayable, work);
}
inline void k_work_init_delayable(k_work_delayable* work, void (*handler)(k_work*)) {
    work->work.handler = handler; work->pending = false; work->delayMs = 0;
}
inline int k_work_schedule(k_work_delayable* work, uint32_t delayMs) {
    work->pending = true; work->delayMs = delayMs; hostLastScheduledWork = work; return 1;
}
inline int k_work_cancel_delayable(k_work_delayable* work) {
    const bool pending = work->pending; work->pending = false; return pending ? 1 : 0;
}
inline bool k_work_cancel_delayable_sync(k_work_delayable* work, k_work_sync*) {
    const bool pending = work->pending; work->pending = false; return pending;
}
inline bool host_run_delayable(k_work_delayable* work) {
    if (!work->pending || work->work.handler == nullptr) return false;
    work->pending = false; work->work.handler(&work->work); return true;
}

inline int64_t k_uptime_get() { return testUptimeMs; }
#define K_NO_WAIT 0
inline void k_work_init(k_work* work, void (*handler)(k_work*)) { work->handler=handler; }
inline int k_work_submit(k_work* work) { if(work->handler) work->handler(work); return 1; }
