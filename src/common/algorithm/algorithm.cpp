#include "common/algorithm/algorithm.h"

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(algorithm, LOG_LEVEL_INF);

Algorithm& Algorithm::Instance() {
    static Algorithm instance;
    return instance;
}

int Algorithm::Initialize(const AppConfig& config) {
    SetConfig(config);
    mLuxBaseline.Reset();
    mSoundBaseline.Reset();
    mSoundFan.Reset();
    mPreviousTarget = {false, config.minBrightness, config.maxCCTMireds, false, 0, 0.0f};
    LOG_INF("Algorithm ready");
    return 0;
}

void Algorithm::SetConfig(const AppConfig& config) {
    mConfig = config;
}

ControlTarget Algorithm::Evaluate(const SensorSnapshot& snapshot) {
    if (snapshot.illuminanceValid) {
        mLuxBaseline.Add(snapshot.lux);
    }
    if (snapshot.sound.valid) {
        mSoundBaseline.Add(snapshot.sound.energy);
    }

    const float lightResidual = snapshot.illuminanceValid ? mLuxBaseline.Residual(snapshot.lux) : 0.0f;
    const float soundResidual = snapshot.sound.valid ? mSoundBaseline.Residual(snapshot.sound.energy) : 0.0f;
    const float score = (mConfig.lightWeight * lightResidual) + (mConfig.soundWeight * soundResidual);
    const float limitedScore = ClampValue(score, 0.0f, 8.0f);

    ControlTarget target = mPreviousTarget;
    target.sensoryScore = limitedScore;
    const LightTarget light = mLightPresence.Evaluate(snapshot, mConfig, limitedScore);
    const uint8_t fanPercent = mSoundFan.Evaluate(snapshot, mConfig, k_uptime_get_32());

    target.lightOn = light.on;
    target.brightnessPercent = light.brightnessPercent;
    target.cctMireds = light.cctMireds;
    target.fanPercent = fanPercent;
    target.fanOn = fanPercent > 0;

    mPreviousTarget = target;
    return target;
}
