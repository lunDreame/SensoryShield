#include "peripheral/Mic.h"

#include <errno.h>
#include <math.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/audio/dmic.h>

LOG_MODULE_REGISTER(mic, LOG_LEVEL_INF);

namespace {
#define MIC_SAMPLE_RATE_HZ 16000U
#define MIC_WINDOW_MS 100U
#define MIC_SAMPLES_PER_WINDOW (MIC_SAMPLE_RATE_HZ * MIC_WINDOW_MS / 1000U)
#define MIC_BLOCK_BYTES (MIC_SAMPLES_PER_WINDOW * sizeof(int16_t))
#define MIC_BUFFER_COUNT 8U
#define MIC_CAPTURE_STACK_SIZE 3072U
#define MIC_PDM_CLK_MIN_HZ 1000000U
#define MIC_PDM_CLK_MAX_HZ 3250000U
#define MIC_PDM_CHANNEL PDM_CHAN_LEFT

K_MEM_SLAB_DEFINE_STATIC(micBuffers, MIC_BLOCK_BYTES, MIC_BUFFER_COUNT, 4);
K_THREAD_STACK_DEFINE(micCaptureStack, MIC_CAPTURE_STACK_SIZE);
struct k_thread micCaptureThread;

#if DT_NODE_HAS_STATUS(DT_ALIAS(dmic_dev), okay)
const struct device* const dmicDevice = DEVICE_DT_GET(DT_ALIAS(dmic_dev));
#endif

void BuildDmicConfig(struct pcm_stream_cfg& stream, struct dmic_cfg& config, enum pdm_lr channel) {
    stream = {
        .pcm_rate = MIC_SAMPLE_RATE_HZ,
        .pcm_width = 16U,
        .block_size = MIC_BLOCK_BYTES,
        .mem_slab = &micBuffers,
    };
    config = {
        .io = {
            .min_pdm_clk_freq = MIC_PDM_CLK_MIN_HZ,
            .max_pdm_clk_freq = MIC_PDM_CLK_MAX_HZ,
            .min_pdm_clk_dc = 40U,
            .max_pdm_clk_dc = 60U,
        },
        .streams = &stream,
        .channel = {
            .req_chan_map_lo = dmic_build_channel_map(0U, 0U, channel),
            .req_chan_map_hi = 0U,
            .req_num_chan = 1U,
            .req_num_streams = 1U,
        },
    };
}

int ConfigureDmicCapture(enum pdm_lr channel) {
#if DT_NODE_HAS_STATUS(DT_ALIAS(dmic_dev), okay)
    struct pcm_stream_cfg stream = {};
    struct dmic_cfg config = {};
    BuildDmicConfig(stream, config, channel);
    return dmic_configure(dmicDevice, &config);
#else
    ARG_UNUSED(channel);
    return -ENODEV;
#endif
}

int StartDmicCapture(enum pdm_lr channel) {
#if DT_NODE_HAS_STATUS(DT_ALIAS(dmic_dev), okay)
    int ret = ConfigureDmicCapture(channel);
    if (ret != 0) {
        LOG_ERR("PDM configure failed: %d", ret);
        return ret;
    }

    ret = dmic_trigger(dmicDevice, DMIC_TRIGGER_START);
    if (ret != 0) {
        LOG_ERR("PDM start failed: %d", ret);
        return ret;
    }

    return 0;
#else
    ARG_UNUSED(channel);
    return -ENODEV;
#endif
}
}  // namespace

Mic& Mic::Instance() {
    static Mic instance;
    return instance;
}

int Mic::Initialize() {
    k_mutex_init(&mLock);
    mFeatures = {0.0f, 0.0f, 0.0f, k_uptime_get_32(), false};
    mHealthy = false;

#if DT_NODE_HAS_STATUS(DT_ALIAS(dmic_dev), okay)
    if (!device_is_ready(dmicDevice)) {
        LOG_ERR("PDM device not ready");
        return -ENODEV;
    }

    const int ret = StartDmicCapture(MIC_PDM_CHANNEL);
    if (ret != 0) {
        return ret;
    }

    k_thread_create(&micCaptureThread, micCaptureStack, K_THREAD_STACK_SIZEOF(micCaptureStack),
                    CaptureThread, this, nullptr, nullptr, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    k_thread_name_set(&micCaptureThread, "sensory_mic");
    LOG_INF("PDM capture ready: %u Hz, %u ms windows", MIC_SAMPLE_RATE_HZ, MIC_WINDOW_MS);
    return 0;
#else
    LOG_ERR("PDM devicetree alias missing");
    return -ENODEV;
#endif
}

SoundFeatures Mic::LatestFeatures() const {
    k_mutex_lock(&mLock, K_FOREVER);
    const SoundFeatures features = mFeatures;
    k_mutex_unlock(&mLock);
    return features;
}

bool Mic::Healthy() const {
    k_mutex_lock(&mLock, K_FOREVER);
    const bool healthy = mHealthy;
    k_mutex_unlock(&mLock);
    return healthy;
}

void Mic::CaptureThread(void* first, void* second, void* third) {
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    static_cast<Mic*>(first)->CaptureLoop();
}

void Mic::CaptureLoop() {
#if DT_NODE_HAS_STATUS(DT_ALIAS(dmic_dev), okay)
    while (true) {
        void* buffer = nullptr;
        size_t size = 0U;
        const int ret = dmic_read(dmicDevice, 0U, &buffer, &size, SYS_FOREVER_MS);
        if (ret != 0) {
            k_mutex_lock(&mLock, K_FOREVER);
            mHealthy = false;
            mFeatures.valid = false;
            k_mutex_unlock(&mLock);
            LOG_ERR("PDM read failed: %d", ret);
            continue;
        }

        if (buffer != nullptr && size >= sizeof(int16_t)) {
            SubmitPcmSamples(static_cast<const int16_t*>(buffer), size / sizeof(int16_t));
        }
        if (buffer != nullptr) {
            k_mem_slab_free(&micBuffers, buffer);
        }
    }
#endif
}

void Mic::SubmitPcmSamples(const int16_t* samples, size_t sampleCount) {
    if (samples == nullptr || sampleCount == 0U) {
        return;
    }

    float sumSquares = 0.0f;
    float peak = 0.0f;
    for (size_t index = 0; index < sampleCount; ++index) {
        const float normalized = static_cast<float>(samples[index]) / 32768.0f;
        const float magnitude = fabsf(normalized);
        sumSquares += normalized * normalized;
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    k_mutex_lock(&mLock, K_FOREVER);
    const float previousEnergy = mFeatures.energy;
    const float energy = sqrtf(sumSquares / static_cast<float>(sampleCount));
    mFeatures = {energy, peak, fabsf(energy - previousEnergy), k_uptime_get_32(), true};
    mHealthy = true;
    k_mutex_unlock(&mLock);
}
