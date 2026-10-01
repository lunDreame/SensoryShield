#pragma once

#include "common/algorithm/baseline.h"
#include "definition.h"

class Algorithm final {
  public:
    static Algorithm& Instance();

    int Initialize(const AppConfig& config);
    void SetConfig(const AppConfig& config);
    ControlTarget Evaluate(const SensorSnapshot& snapshot);

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;

  private:
    Algorithm() = default;
    ~Algorithm() = default;

    AppConfig mConfig = {};
    RollingBaseline mLuxBaseline;
    RollingBaseline mSoundBaseline;
    ControlTarget mPreviousTarget = {};
};

inline Algorithm* GetAlgorithm() {
    return &Algorithm::Instance();
}
