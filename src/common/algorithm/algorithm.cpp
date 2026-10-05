#include "common/algorithm/algorithm.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(algorithm, LOG_LEVEL_INF);

Algorithm& Algorithm::Instance() {
    static Algorithm instance;
    return instance;
}

int Algorithm::Initialize(const AppConfig& config) {
    SetConfig(config);
    mStimulusScorer.Reset();
    mSoundFan.Reset();
    mPreviousTarget = {false, config.minBrightness, config.maxCCTMireds, false, 255, 255, 255, false, 0, 0.0f};
    LOG_INF("Algorithm ready");
    return 0;
}

void Algorithm::SetConfig(const AppConfig& config) {
    mConfig = config;
}

ControlTarget Algorithm::Evaluate(const SensorSnapshot& snapshot) {
    const StimulusScore stimulus = mStimulusScorer.Evaluate(snapshot, mConfig);

    ControlTarget target = mPreviousTarget;
    target.sensoryScore = stimulus.combined;
    const LightTarget light = mLightPresence.Evaluate(snapshot, mConfig, stimulus.combined);
    const uint8_t fanPercent = mSoundFan.Evaluate(snapshot, mConfig, k_uptime_get_32());

    target.lightOn = light.on;
    target.brightnessPercent = light.brightnessPercent;
    target.cctMireds = light.cctMireds;
    target.rgbMode = false;
    target.red = 255;
    target.green = 255;
    target.blue = 255;
    target.fanPercent = fanPercent;
    target.fanOn = fanPercent > 0;

    mPreviousTarget = target;
    return target;
}
