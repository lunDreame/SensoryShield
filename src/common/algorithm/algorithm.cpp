#include "common/algorithm/algorithm.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(algorithm, LOG_LEVEL_INF);

namespace {
uint8_t NormalBrightness(const AppConfig& c) {
    return ClampValue(c.preferredBrightness, c.minBrightness, c.maxBrightness);
}
uint16_t NormalCct(const AppConfig& c) {
    return ClampValue(c.normalCCTMireds, c.minCCTMireds, c.maxCCTMireds);
}
} // namespace

Algorithm& Algorithm::Instance() {
    static Algorithm instance;
    return instance;
}

int Algorithm::Initialize(const AppConfig& config) {
    mConfig = config;
    mLightRebasePending = false;
    mStimulusScorer.Reset();
    mSoundFan.Reset();
    mOutputAware.Reset();
    mLightPresence.Reset();
    mPreviousTarget = {false, config.minBrightness, config.maxCCTMireds, false, 255, 255, 255, false, 0, 0.0f};
    LOG_INF("Algorithm ready");
    return 0;
}

void Algorithm::SetConfig(const AppConfig& config) {
    // A saved default-output edit establishes a new operating context. Temporary
    // manual controls and relief-tone edits never take this path into rebasing.
    if (NormalBrightness(config) != NormalBrightness(mConfig) || NormalCct(config) != NormalCct(mConfig)) {
        mLightRebasePending = true;
        mLightPresence.OnDefaultOutputChanged();
    }
    mConfig = config;
}

StimulusScore Algorithm::EvaluateEnvironment(const SensorSnapshot& snapshot, bool automaticMode) {
    mObserved = mOutputAware.Observe(snapshot);
    SensorSnapshot learning = mObserved;
    bool ordinaryOutput = mLightPresence.IsNormal() && mSoundFan.IsNormal();
    if (snapshot.observedOutputs.valid) {
        const auto& o = snapshot.observedOutputs;
        const uint8_t normalBrightness = ClampValue(mConfig.preferredBrightness, mConfig.minBrightness, mConfig.maxBrightness);
        const uint16_t normalCct = ClampValue(mConfig.normalCCTMireds, mConfig.minCCTMireds, mConfig.maxCCTMireds);
        const uint8_t normalFan = ClampValue<uint8_t>(mConfig.preferredFanPercent, 0, mConfig.fanMaxPercent);
        ordinaryOutput = ordinaryOutput && o.lightOn == (normalBrightness > 0) && o.brightness == normalBrightness &&
            !o.rgb && o.cct == normalCct && o.fanOn == (normalFan > 0) && o.fanPercent == normalFan;
    }
    // Keep the old baseline until the NEW normal output is actually observed,
    // fresh and settled in AUTO. An explicit rebase seeds only lux, never sound.
    const bool rebased = mLightRebasePending && automaticMode && ordinaryOutput &&
        snapshot.observedOutputs.valid && mObserved.controlInputsSettled &&
        mObserved.illuminanceValid && mStimulusScorer.RebaseLight(mObserved.lux);
    if (rebased) mLightRebasePending = false;
    learning.learnLight = learning.learnLight && automaticMode && ordinaryOutput;
    learning.learnSound = learning.learnSound && automaticMode && ordinaryOutput;
    // Do not count the seed twice or admit measurements from an earlier output.
    learning.learnLight = learning.learnLight && !mLightRebasePending && !rebased;
    StimulusScore score = mStimulusScorer.Evaluate(learning, mConfig);
    if (mLightRebasePending) {
        score.baselineReady = false;
        if (automaticMode) {
            // The old lux reference is not comparable with the newly requested
            // output. Keep sound protection active while applying/recovering to
            // the new default; otherwise our own light can prevent rebasing.
            score.light = 0;
            score.combined = ClampValue(score.sound, 0.0f, STIMULUS_MAX_SCORE);
        }
    }
    return score;
}

ControlTarget Algorithm::Evaluate(const SensorSnapshot& snapshot, const StimulusScore& stimulus) {
    SensorSnapshot effectiveSnapshot = snapshot.observedOutputs.valid ? mObserved : snapshot;
    const uint32_t now = k_uptime_get_32();
    if (!mConfig.occupancyAutomationEnabled) {
        // Bypass only automatic shutdown; raw PIR state remains visible in telemetry/Matter.
        effectiveSnapshot.occupied = true;
    } else if (snapshot.pirValid && !snapshot.occupied && snapshot.lastMotionMs != 0U &&
        (now - snapshot.lastMotionMs) < mConfig.occupancyTimeoutMs) {
        effectiveSnapshot.occupied = true;
    }

    ControlTarget target = mPreviousTarget;
    target.sensoryScore = stimulus.combined;
    const LightTarget light = mLightPresence.Evaluate(effectiveSnapshot, mConfig, stimulus.combined, now);
    const uint8_t fanPercent = mSoundFan.Evaluate(effectiveSnapshot, mConfig, stimulus, now);

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
