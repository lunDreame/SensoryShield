#pragma once
#include <stddef.h>
typedef int (*settings_read_cb)(void*, void*, size_t);
typedef int (*settings_load_direct_cb)(const char*, size_t, settings_read_cb, void*, void*);
int settings_subsys_init();
int settings_load_subtree_direct(const char*, settings_load_direct_cb, void*);
int settings_save_one(const char*, const void*, size_t);
int settings_delete(const char*);
