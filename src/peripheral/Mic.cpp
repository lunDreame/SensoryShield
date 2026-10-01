#include "peripheral/Mic.h"

#include <errno.h>
#include <math.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mic, LOG_LEVEL_INF);

namespace {
#define MIC_SAMPLE_RATE_HZ 16000U
#define MIC_WINDOW_MS 100U
#define MIC_SAMPLES_PER_WINDOW (MIC_SAMPLE_RATE_HZ * MIC_WINDOW_MS / 1000U)
#define MIC_BLOCK_BYTES (MIC_SAMPLES_PER_WINDOW * sizeof(int16_t))
#define MIC_BUFFER_COUNT 4U
#define MIC_READ_TIMEOUT_MS 250
#define MIC_CAPTURE_STACK_SIZE 2048U

K_MEM_SLAB_DEFINE_STATIC(micBuffers, MIC_BLOCK_BYTES, MIC_BUFFER_COUNT, 4);
K_THREAD_STACK_DEFINE(micCaptureStack, MIC_CAPTURE_STACK_SIZE);
struct k_thread micCaptureThread;

#if DT_NODE_HAS_STATUS(DT_ALIAS(dmic_dev), okay)
const struct device* const dmicDevice = DEVICE_DT_GET(DT_ALIAS(dmic_dev));
#endif
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

    struct pcm_stream_cfg stream = {
        .pcm_rate = MIC_SAMPLE_RATE_HZ,
        .pcm_width = 16U,
        .block_size = MIC_BLOCK_BYTES,
        .mem_slab = &micBuffers,
    };
    struct dmic_cfg config = {
        .io = {
            .min_pdm_clk_freq = 1000000U,
            .max_pdm_clk_freq = 3500000U,
            .min_pdm_clk_dc = 40U,
            .max_pdm_clk_dc = 60U,
        },
        .streams = &stream,
        .channel = {
            .req_chan_map_lo = dmic_build_channel_map(0U, 0U, PDM_CHAN_LEFT),
            .req_chan_map_hi = 0U,
            .req_num_chan = 1U,
            .req_num_streams = 1U,
        },
    };

    int ret = dmic_configure(dmicDevice, &config);
    if (ret != 0) {
        LOG_ERR("PDM configure failed: %d", ret);
        return ret;
    }

    ret = dmic_trigger(dmicDevice, DMIC_TRIGGER_START);
    if (ret != 0) {
        LOG_ERR("PDM start failed: %d", ret);
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
    uint32_t consecutiveErrors = 0U;
    while (true) {
        void* buffer = nullptr;
        size_t size = 0U;
        const int ret = dmic_read(dmicDevice, 0U, &buffer, &size, MIC_READ_TIMEOUT_MS);
        if (ret != 0) {
            ++consecutiveErrors;
            k_mutex_lock(&mLock, K_FOREVER);
            mHealthy = false;
            mFeatures.valid = false;
            k_mutex_unlock(&mLock);

            if (consecutiveErrors == 1U || (consecutiveErrors % 10U) == 0U) {
                LOG_WRN("PDM read failed: %d (%u consecutive)", ret, consecutiveErrors);
            }

            const int stopRet = dmic_trigger(dmicDevice, DMIC_TRIGGER_STOP);
            const int startRet = dmic_trigger(dmicDevice, DMIC_TRIGGER_START);
            if (stopRet != 0 || startRet != 0) {
                LOG_ERR("PDM recovery failed: stop=%d start=%d", stopRet, startRet);
                k_sleep(K_MSEC(500));
            }
            continue;
        }

        consecutiveErrors = 0U;
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
