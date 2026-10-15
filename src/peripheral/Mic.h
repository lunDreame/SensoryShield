#pragma once

#include "definition.h"

#include <stddef.h>
#include <zephyr/kernel.h>

class Mic final {
  public:
    static constexpr uint32_t MaxSampleAgeMs = 1000;
    struct Diagnostics {
        uint32_t sampleCount, sampleAgeMs, restartCount;
        int lastError;
        bool fresh, recovering;
    };
    static Mic& Instance();

    int Initialize();
    SoundFeatures LatestFeatures() const;
    bool Healthy() const;
    Diagnostics GetDiagnostics() const;

    Mic(const Mic&) = delete;
    Mic& operator=(const Mic&) = delete;

  private:
    Mic() = default;
    ~Mic() = default;

    static void CaptureThread(void* first, void* second, void* third);
    void CaptureLoop();
    int CaptureStep();
    int RestartCapture(int error);
    void SubmitPcmSamples(const int16_t* samples, size_t sampleCount);

    SoundFeatures mFeatures = {};
    bool mHealthy = false;
    bool mRecovering = false;
    uint32_t mSampleCount = 0, mRestartCount = 0;
    int mLastError = 0;
    mutable struct k_mutex mLock;
};

inline Mic* GetMic() {
    return &Mic::Instance();
}
