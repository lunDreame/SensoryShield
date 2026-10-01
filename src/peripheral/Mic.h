#pragma once

#include "definition.h"

#include <stddef.h>
#include <zephyr/kernel.h>

class Mic final {
  public:
    static Mic& Instance();

    int Initialize();
    SoundFeatures LatestFeatures() const;
    bool Healthy() const;

    Mic(const Mic&) = delete;
    Mic& operator=(const Mic&) = delete;

  private:
    Mic() = default;
    ~Mic() = default;

    static void CaptureThread(void* first, void* second, void* third);
    void CaptureLoop();
    void SubmitPcmSamples(const int16_t* samples, size_t sampleCount);

    SoundFeatures mFeatures = {};
    bool mHealthy = false;
    mutable struct k_mutex mLock;
};

inline Mic* GetMic() {
    return &Mic::Instance();
}
