#pragma once

#include <stdint.h>

class PIR final {
  public:
    static PIR& Instance();

    int Initialize(uint32_t occupancyTimeoutMs);
    void Poll();
    bool IsOccupied() const {
        return mOccupied;
    }
    bool Healthy() const {
        return mHealthy;
    }
    uint32_t LastMotionMs() const {
        return mLastMotionMs;
    }

    PIR(const PIR&) = delete;
    PIR& operator=(const PIR&) = delete;

  private:
    PIR() = default;
    ~PIR() = default;

    uint32_t mOccupancyTimeoutMs = 0;
    uint32_t mLastMotionMs = 0;
    bool mOccupied = false;
    bool mHealthy = false;
};

inline PIR* GetPIR() {
    return &PIR::Instance();
}
