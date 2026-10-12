#pragma once
#include "system/memory.h"
#include <zephyr/settings/settings.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <map>
#include <string>
#include <vector>
#include <cstdio>

static std::map<std::string, std::vector<unsigned char>> storage;
static bool failSave = false;
int settings_subsys_init() { return 0; }
static ssize_t Read(void* source, void* target, size_t length) {
    auto& bytes = *static_cast<std::vector<unsigned char>*>(source);
    memcpy(target, bytes.data(), length);
    return length;
}
int settings_load_subtree_direct(const char* key, settings_load_direct_cb cb, void* param) {
    auto found = storage.find(key);
    if (found == storage.end()) return 0;
    const int ret = cb(nullptr, found->second.size(), Read, &found->second, param);
    return ret < 0 ? ret : 0;
}
int settings_save_one(const char* key, const void* data, size_t length) {
    if (failSave) return -EIO;
    const auto* bytes = static_cast<const unsigned char*>(data);
    storage[key] = {bytes, bytes + length};
    return 0;
}
int settings_delete(const char* key) { return storage.erase(key) ? 0 : -ENOENT; }
