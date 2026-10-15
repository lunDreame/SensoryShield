#pragma once
#include_next <zephyr/kernel.h>
#define ARG_UNUSED(value) (void)(value)
struct k_mem_slab {};
struct k_thread {};
inline int hostMicFreed = 0;
#define K_MEM_SLAB_DEFINE_STATIC(name, ...) static k_mem_slab name
#define K_THREAD_STACK_DEFINE(name, size) static unsigned char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define K_PRIO_PREEMPT(value) (value)
inline void k_mem_slab_free(k_mem_slab*, void*) { ++hostMicFreed; }
inline void k_sleep(uint32_t ms) { testUptimeMs += ms; }
inline void* k_thread_create(k_thread*, unsigned char*, size_t,
    void (*)(void*,void*,void*), void*, void*, void*, int, int, int) { return nullptr; }
inline void k_thread_name_set(k_thread*, const char*) {}
