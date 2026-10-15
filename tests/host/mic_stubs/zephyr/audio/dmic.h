#pragma once
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <cassert>
#include <cerrno>
#include <deque>
inline device hostMicDevice;
#define DEVICE_DT_GET(node) (&hostMicDevice)
enum pdm_lr { PDM_CHAN_LEFT, PDM_CHAN_RIGHT };
enum dmic_trigger { DMIC_TRIGGER_STOP, DMIC_TRIGGER_START };
struct pcm_stream_cfg { uint32_t pcm_rate; uint8_t pcm_width; uint16_t block_size; k_mem_slab* mem_slab; };
struct dmic_cfg {
    struct { uint32_t min_pdm_clk_freq, max_pdm_clk_freq; uint8_t min_pdm_clk_dc, max_pdm_clk_dc; } io;
    pcm_stream_cfg* streams;
    struct { uint32_t req_chan_map_lo, req_chan_map_hi; uint8_t req_num_chan, req_num_streams; } channel;
};
inline uint32_t dmic_build_channel_map(int, int, pdm_lr channel) { return channel; }
struct HostMicRead { int result; void* buffer; size_t size; };
inline std::deque<HostMicRead> hostMicReads;
inline int hostMicStarts = 0, hostMicStops = 0, hostMicConfigureError = 0;
inline int dmic_configure(const device*, dmic_cfg* c) {
    assert(c->streams->pcm_rate == 16000 && c->streams->block_size == 3200);
    return hostMicConfigureError;
}
inline int dmic_trigger(const device*, enum dmic_trigger command) {
    if (command == DMIC_TRIGGER_START) ++hostMicStarts; else ++hostMicStops;
    return 0;
}
inline int dmic_read(const device*, uint8_t, void** buffer, size_t* size, int32_t timeout) {
    assert(timeout >= 0 && timeout <= 1000); // Never wait forever after driver stop.
    if (hostMicReads.empty()) { testUptimeMs += timeout; return -EAGAIN; }
    const auto next = hostMicReads.front(); hostMicReads.pop_front();
    if (next.result == 0) { *buffer = next.buffer; *size = next.size; }
    return next.result;
}
