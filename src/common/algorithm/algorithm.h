#pragma once

#include "common/algorithm/sensory_relief.h"
#include "common/algorithm/stimulus_score.h"
#include "common/algorithm/output_aware_inputs.h"
#include "definition.h"
#include "common/algorithm/timed_relief.h"

class Algorithm final {
  public:
    static Algorithm& Instance();

    int Initialize(const AppConfig& config);
    void SetConfig(const AppConfig& config);
    void SeedEnvironment(const InitialEnvironment& initial) {
        mStimulusScorer.SeedEnvironment(initial);
    }
    // Score every mode, but admit ordinary samples only in settled automatic normal output.
    StimulusScore EvaluateEnvironment(const SensorSnapshot& snapshot, bool automaticMode = true);
    ControlTarget Evaluate(const SensorSnapshot& snapshot, const StimulusScore& stimulus);
    void ResetAutomaticLight() { mRelief.Reset(); mTimed.Reset(); mTimedChild = false; }
    void StartChild(ControlTarget current, ControlTarget desired, uint32_t now) {
        mRelief.Reset(); mTimed.StartChild(current,desired,now); mTimedChild=true;
    }
    bool ChildHoldComplete(uint32_t now) const { return mTimed.ChildHoldComplete(now); }
    uint32_t ChildRemainingSeconds(uint32_t now) const { return mTimed.ChildRemainingSeconds(now); }
    ControlTarget EvaluateChild(const SensorSnapshot& snapshot,const StimulusScore& stimulus);
    bool InstallRehearsalBaseline(const InitialEnvironment& initial);
    bool BaselineFrozen() const { return mBaselineFrozen; }
    float LightBaselineMedian() const { return mStimulusScorer.LightBaselineMedian(); }
    float LightBaselineMad() const { return mStimulusScorer.LightBaselineMad(); }
    float SoundBaselineMedian() const { return mStimulusScorer.SoundBaselineMedian(); }
    bool LightRebasePending() const { return mLightRebasePending; }
    bool SoundRebasePending() const { return mSoundRebasePending; }
    bool ControlInputsSettled() const { return mObserved.controlInputsSettled; }

    Algorithm(const Algorithm&) = delete;
    Algorithm& operator=(const Algorithm&) = delete;

  private:
    Algorithm() = default;
    ~Algorithm() = default;

    AppConfig mConfig = {};
    bool mLightRebasePending = false;
    bool mSoundRebasePending = false;
    OutputAwareInputs mOutputAware;
    SensorSnapshot mObserved = {};
    StimulusScorer mStimulusScorer;
    SensoryReliefController mRelief;
    TimedReliefController mTimed;
    bool mTimedChild = false, mBaselineFrozen = false;
    ControlTarget mPreviousTarget = {};
};

inline Algorithm* GetAlgorithm() {
    return &Algorithm::Instance();
}
