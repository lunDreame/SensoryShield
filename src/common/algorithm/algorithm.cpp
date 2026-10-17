#include "common/algorithm/algorithm.h"

#include "demo/environment_samples.h"
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
    mSoundRebasePending = false;
    mStimulusScorer.Reset();
    mOutputAware.Reset();
    mObserved = {};
    mObserved.controlInputsSettled = false;
    mRelief.Reset();
    mTimed.Reset(); mTimedChild=false; mBaselineFrozen=false;
    if (RehearsalBaseline::Enabled) InstallRehearsalBaseline(RehearsalBaseline::Samples);
    mPreviousTarget = {false, config.minBrightness, NormalCct(config), false, 255, 255, 255, false, 0, 0.0f};
    LOG_INF("Algorithm ready");
    return 0;
}

bool Algorithm::InstallRehearsalBaseline(const InitialEnvironment& initial) {
#if DEMO_DEFAULTS
    if (!initial.Valid() || !RehearsalBaseline::Matches(mConfig)) return false;
    mStimulusScorer.SeedEnvironment(initial); mBaselineFrozen=true; return true;
#else
    (void)initial; return false; // Everyday operation always learns live samples.
#endif
}

void Algorithm::SetConfig(const AppConfig& config) {
    // A saved default-output edit establishes a new operating context. Temporary
    // manual controls and relief-tone edits never take this path into rebasing.
    if (NormalBrightness(config) != NormalBrightness(mConfig) || NormalCct(config) != NormalCct(mConfig)) {
        mLightRebasePending = true;
        mRelief.OnDefaultOutputChanged();
    }
    if (NormalizeFanPercent(config.preferredFanPercent, config.fanMaxPercent) !=
        NormalizeFanPercent(mConfig.preferredFanPercent, mConfig.fanMaxPercent)) {
        mSoundRebasePending = true;
        mRelief.OnDefaultOutputChanged();
    }
    if (mBaselineFrozen && !RehearsalBaseline::Matches(config)) {
        mBaselineFrozen=false; mStimulusScorer.Reset();
    }
    mTimed.OnConfigChanged(config,k_uptime_get_32()); mTimedChild=false;
    mConfig = config;
}

StimulusScore Algorithm::EvaluateEnvironment(const SensorSnapshot& snapshot, bool automaticMode) {
    mObserved = mOutputAware.Observe(snapshot);
    SensorSnapshot learning = mObserved;
    bool ordinaryOutput = (DEMO_DEFAULTS || mTimedChild) ? mTimed.IsNormal() : mRelief.IsNormal();
    if (snapshot.observedOutputs.valid) {
        const auto& o = snapshot.observedOutputs;
        const uint8_t normalBrightness = ClampValue(mConfig.preferredBrightness, mConfig.minBrightness, mConfig.maxBrightness);
        const uint16_t normalCct = ClampValue(mConfig.normalCCTMireds, mConfig.minCCTMireds, mConfig.maxCCTMireds);
        const uint8_t normalFan = NormalizeFanPercent(mConfig.preferredFanPercent, mConfig.fanMaxPercent);
        ordinaryOutput = ordinaryOutput && o.lightOn == (normalBrightness > 0) && o.brightness == normalBrightness &&
            !o.rgb && o.cct == normalCct && o.fanOn == (normalFan > 0) && o.fanPercent == normalFan;
    }
    // Rebase only the channel whose saved normal output changed, once the new
    // output is fresh and settled in AUTO. Temporary controls never rebase.
    const bool rebased = mLightRebasePending && automaticMode && ordinaryOutput &&
        snapshot.observedOutputs.valid && mObserved.controlInputsSettled &&
        mObserved.illuminanceValid && mStimulusScorer.RebaseLight(mObserved.lux);
    if (rebased) mLightRebasePending = false;
    const bool soundRebased = mSoundRebasePending && automaticMode && ordinaryOutput &&
        snapshot.observedOutputs.valid && mObserved.controlInputsSettled &&
        mObserved.micValid && mObserved.sound.valid && mStimulusScorer.RebaseSound(mObserved.sound.energy);
    if (soundRebased) mSoundRebasePending = false;
    learning.learnLight = learning.learnLight && !mBaselineFrozen && automaticMode && ordinaryOutput;
    learning.learnSound = learning.learnSound && !mBaselineFrozen && automaticMode && ordinaryOutput;
    // Do not count the seed twice or admit measurements from an earlier output.
    learning.learnLight = learning.learnLight && !mLightRebasePending && !rebased;
    learning.learnSound = learning.learnSound && !mSoundRebasePending && !soundRebased;
    StimulusScore score = mStimulusScorer.Evaluate(learning, mConfig);
    if (mLightRebasePending || mSoundRebasePending) {
        score.baselineReady = false;
        if (mSoundRebasePending) score.soundBaselineReady = false;
        if (automaticMode) {
            // Preserve the unchanged channel's protection while the old reference
            // for an explicitly edited output is temporarily incomparable.
            if (mLightRebasePending) score.light = 0;
            if (mSoundRebasePending) score.sound = 0;
            score.combined = ClampValue(score.light + score.sound, 0.0f, STIMULUS_MAX_SCORE);
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

    if (DEMO_DEFAULTS || mTimedChild) {
        mPreviousTarget=mTimed.Evaluate(effectiveSnapshot,mConfig,stimulus.combined,now);
        if (mTimed.IsNormal()) mTimedChild=false;
        return mPreviousTarget;
    }
    ControlTarget target = mPreviousTarget;
    target.sensoryScore = stimulus.combined;
    const SensoryTarget light = mRelief.Evaluate(effectiveSnapshot, mConfig, stimulus.combined, now);

    target.lightOn = light.on;
    target.brightnessPercent = light.brightnessPercent;
    target.cctMireds = light.cctMireds;
    target.rgbMode = false;
    target.red = 255;
    target.green = 255;
    target.blue = 255;
    target.fanPercent = light.fanPercent;
    target.fanOn = light.fanPercent > 0;

    mPreviousTarget = target;
    return target;
}

ControlTarget Algorithm::EvaluateChild(const SensorSnapshot& snapshot,const StimulusScore& stimulus) {
    const auto& effective=snapshot.observedOutputs.valid ? mObserved : snapshot;
    mPreviousTarget=mTimed.Evaluate(effective,mConfig,stimulus.combined,k_uptime_get_32(),false);
    return mPreviousTarget;
}
