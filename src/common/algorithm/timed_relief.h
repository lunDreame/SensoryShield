#pragma once
#include "common/algorithm/recovery_guard.h"
#include "common/algorithm/stimulus_policy.h"
#include <cmath>

// Demonstration automatic episodes and timed child requests. The everyday
// automatic controller retains its score-proportional targets and step recovery.
class TimedReliefController final {
  public:
    enum class Phase { Normal, Adjust, Hold, Recover };
    void Reset() {
        *this = {};
    }
    bool IsNormal() const {
        return mPhase == Phase::Normal;
    }
    Phase CurrentPhase() const {
        return mPhase;
    }
    void OnConfigChanged(const AppConfig& c, uint32_t now) {
        mRetry.Reset();
        mHigh = mLow = false;
        if (mChild) {
            Reset();
            return;
        }
        if (IsNormal())
            return;
        // Applying new limits/tone cannot silently brighten a relief episode.
        mCurrent.brightnessPercent = ClampValue<uint8_t>(mCurrent.brightnessPercent, 0, c.maxBrightness);
        mCurrent.fanPercent = NormalizeFanPercent(mCurrent.fanPercent, c.fanMaxPercent);
        mCurrent.lightOn = mCurrent.brightnessPercent > 0;
        mCurrent.fanOn = mCurrent.fanPercent > 0;
        mCurrent.cctMireds = ClampValue(mCurrent.cctMireds, c.minCCTMireds, c.maxCCTMireds);
        if (mPhase == Phase::Recover) {
            mFrom = mCurrent;
            mRecoveryElapsed = 0;
            mPaused = true;
        } else {
            mHoldMs = c.reliefHoldMs;
            StartAdjust(mCurrent, Relief(c), now);
        }
    }
    static ControlTarget Normal(const AppConfig& c) {
        const auto light = ClampValue(c.preferredBrightness, c.minBrightness, c.maxBrightness);
        const auto fan = NormalizeFanPercent(c.preferredFanPercent, c.fanMaxPercent);
        return {light > 0, light,   ClampValue(c.normalCCTMireds, c.minCCTMireds, c.maxCCTMireds),
                false,     255,     255,
                255,       fan > 0, fan,
                0};
    }
    static ControlTarget Relief(const AppConfig& c) {
        auto t = Normal(c);
        t.brightnessPercent = c.minBrightness;
        t.lightOn = t.brightnessPercent > 0;
        t.cctMireds = ClampValue(c.preferredCCTMireds, c.minCCTMireds, c.maxCCTMireds);
        t.fanPercent = ClampValue<uint8_t>(NormalizeFanPercent(c.reliefFanPercent, c.fanMaxPercent), 0, t.fanPercent);
        t.fanOn = t.fanPercent > 0;
        return t;
    }
    static ControlTarget ChildTarget(ComfortAction action, ControlTarget current, const AppConfig& c) {
        if (!current.lightOn)
            current.brightnessPercent = 0;
        if (!current.fanOn)
            current.fanPercent = 0;
        auto t = current;
        if (action == ComfortAction::Bright || action == ComfortAction::Loud) {
#if SENSORYSHIELD_DEMO_DEFAULTS
            t.brightnessPercent =
                current.brightnessPercent < c.minBrightness ? current.brightnessPercent : c.minBrightness;
#else
            t.brightnessPercent = static_cast<uint8_t>(ClampValue<int>(
                current.brightnessPercent - CHILD_OUTPUT_STEP_PERCENT,
                current.brightnessPercent < c.minBrightness ? current.brightnessPercent : c.minBrightness,
                c.maxBrightness));
#endif
        } else if (action == ComfortAction::Dark) {
            t.brightnessPercent =
                current.brightnessPercent >= c.maxBrightness
                    ? current.brightnessPercent
                    : static_cast<uint8_t>(ClampValue<int>(current.brightnessPercent + CHILD_OUTPUT_STEP_PERCENT,
                                                           c.minBrightness, c.maxBrightness));
        }
        if (t.brightnessPercent != current.brightnessPercent) {
            t.cctMireds = ClampValue(c.preferredCCTMireds, c.minCCTMireds, c.maxCCTMireds);
            t.rgbMode = false;
            t.red = t.green = t.blue = 255;
        }
        if (action == ComfortAction::Loud) {
#if SENSORYSHIELD_DEMO_DEFAULTS
            t.fanPercent =
                ClampValue<uint8_t>(NormalizeFanPercent(c.reliefFanPercent, c.fanMaxPercent), 0, current.fanPercent);
#else
            const int reduced = current.fanPercent - CHILD_OUTPUT_STEP_PERCENT;
            t.fanPercent = reduced < FAN_MIN_DUTY_PERCENT ? 0 : NormalizeFanPercent(reduced, c.fanMaxPercent);
#endif
        } else if (action == ComfortAction::Quiet) {
            t.fanPercent = NormalizeFanPercent(ClampValue<int>(current.fanPercent + CHILD_OUTPUT_STEP_PERCENT, 0, 100),
                                               c.fanMaxPercent);
        }
        t.lightOn = t.brightnessPercent > 0;
        t.fanOn = t.fanPercent > 0;
        return t;
    }
    void StartChild(ControlTarget current, ControlTarget desired, uint32_t now) {
        Reset();
        mChild = true;
        mHoldMs = CHILD_HOLD_MS;
        StartAdjust(current, desired, now);
    }
    bool ChildHoldComplete(uint32_t now) const {
        return mChild && mPhase == Phase::Hold && now - mHoldSince >= mHoldMs;
    }
    uint32_t ChildRemainingSeconds(uint32_t now) const {
        if (!mChild)
            return 0;
        const uint32_t elapsed = mPhase == Phase::Hold ? now - mHoldSince : 0;
        return elapsed >= mHoldMs ? 0 : (mHoldMs - elapsed + 999) / 1000;
    }
    ControlTarget Evaluate(const SensorSnapshot& s, const AppConfig& c, float score, uint32_t now,
                           bool automatic = true) {
        const auto normal = Normal(c);
        if (automatic && c.occupancyAutomationEnabled && (!s.pirValid || !s.occupied)) {
            Reset();
            mCurrent = normal;
            mCurrent.lightOn = mCurrent.fanOn = false;
            mCurrent.brightnessPercent = mCurrent.fanPercent = 0;
            return mCurrent;
        }
        const auto& sound = s.sound;
        const bool trusted = s.illuminanceValid && std::isfinite(s.lux) && s.lux >= 0 && s.micValid && sound.valid &&
                             std::isfinite(score) && score >= 0 && std::isfinite(sound.energy) && sound.energy >= 0 &&
                             sound.energy <= 1 && std::isfinite(sound.peak) && sound.peak >= sound.energy &&
                             sound.peak <= 1 && std::isfinite(sound.delta) && sound.delta >= 0 && sound.delta <= 1 &&
                             now - sound.timestampMs <= 1000 &&
                             (!s.observedOutputs.valid || now - s.observedAtMs <= 1000);
        const bool fresh = trusted && (!mSeen || sound.timestampMs != mLastSample);
        const bool gap = mSeen && trusted && sound.timestampMs - mLastSample > 1000;
        const uint32_t delta = mSeen && !gap && trusted ? now - mLastTick : 0;
        if (fresh) {
            mSeen = true;
            mLastSample = sound.timestampMs;
        }
        mLastTick = now;
        if (!trusted || gap) {
            mHigh = mLow = false;
            mPaused = true;
        }
        if (mPhase == Phase::Normal) {
            mCurrent = normal;
            if (!trusted) {
                if (s.observedOutputs.valid)
                    mCurrent = Observed(s, normal);
                mCurrent.fanPercent = Relief(c).fanPercent;
                mCurrent.fanOn = mCurrent.fanPercent > 0;
            }
            if (fresh && s.controlInputsSettled && !gap &&
                Confirm(score >= StimulusPolicy::EnterScore, mHigh, mHighSince, now, StimulusPolicy::EnterMs)) {
                mHoldMs = c.reliefHoldMs;
                mChild = false;
                StartAdjust(Observed(s, mCurrent), Relief(c), now);
            } else if (!s.controlInputsSettled || !trusted)
                mHigh = false;
        } else if (mPhase == Phase::Adjust) {
            // Progress uses elapsed time, but hold never starts on a requested output alone.
            mCurrent = Blend(mFrom, mTo, now - mAdjustSince, CHILD_TRANSITION_MS, c);
            if (now - mAdjustSince >= CHILD_TRANSITION_MS && Arrived(s, mTo, now)) {
                mCurrent = mTo;
                mPhase = Phase::Hold;
                mHoldSince = now;
                mLow = mHigh = false;
            }
        } else if (mPhase == Phase::Hold) {
            if (!Arrived(s, mCurrent, now)) {
                mLow = false;
                mHoldSince = now;
            }
            if (fresh && !gap) {
                const bool eligible = s.controlInputsSettled && Arrived(s, mCurrent, now);
                const bool retryAllowed = eligible && mRetry.MayRecover(s, true);
                const bool low = Confirm(eligible && score <= StimulusPolicy::RecoverScore, mLow, mLowSince, now,
                                         c.recoverConfirmMs);
                if (automatic && mChild &&
                    Confirm(eligible && score >= StimulusPolicy::EnterScore, mHigh, mHighSince, now,
                            StimulusPolicy::EnterMs)) {
                    mChild = false;
                    mHoldMs = c.reliefHoldMs;
                    StartAdjust(mCurrent, Relief(c), now);
                } else if (automatic && low && now - mHoldSince >= mHoldMs && retryAllowed) {
                    mPhase = Phase::Recover;
                    mFrom = mCurrent;
                    mTo = normal;
                    mRecoveryElapsed = 0;
                    mPaused = false;
                    mHigh = mLow = false;
                    mChild = false;
                }
            }
        } else if (fresh && !gap) {
            // Continue sensing during our output ramp. Learning is separately disabled.
            // Requiring output-settled here would stop the ramp after every step.
            if (score > StimulusPolicy::RecoverScore) {
                mLow = false;
                mPaused = true;
                if (Confirm(score >= StimulusPolicy::EnterScore, mHigh, mHighSince, now, StimulusPolicy::EnterMs)) {
                    mRetry.Block();
                    mHoldMs = c.reliefHoldMs;
                    StartAdjust(mCurrent, Relief(c), now);
                }
            } else {
                mHigh = false;
                if (mPaused) {
                    if (Confirm(s.controlInputsSettled, mLow, mLowSince, now, c.recoverConfirmMs)) {
                        mPaused = false;
                        mLow = false;
                    }
                } else {
                    mRecoveryElapsed = ClampValue<uint32_t>(mRecoveryElapsed + delta, 0, DEMO_TRANSITION_MS);
                    mCurrent = Blend(mFrom, normal, mRecoveryElapsed, DEMO_TRANSITION_MS, c);
                    if (mRecoveryElapsed >= DEMO_TRANSITION_MS && Arrived(s, normal, now) && s.controlInputsSettled) {
                        mPhase = Phase::Normal;
                        mRetry.Reset();
                    }
                }
            }
        }
        mCurrent.sensoryScore = score;
        return mCurrent;
    }

  private:
    Phase mPhase = Phase::Normal;
    ControlTarget mCurrent = {}, mFrom = {}, mTo = {};
    RecoveryGuard mRetry;
    bool mChild = false, mHigh = false, mLow = false, mSeen = false, mPaused = false;
    uint32_t mHighSince = 0, mLowSince = 0, mAdjustSince = 0, mHoldSince = 0, mHoldMs = 0, mRecoveryElapsed = 0,
             mLastSample = 0, mLastTick = 0;
    static bool Confirm(bool condition, bool& pending, uint32_t& since, uint32_t now, uint32_t duration) {
        if (!condition) {
            pending = false;
            return false;
        }
        if (!pending) {
            pending = true;
            since = now;
        }
        return now - since >= duration;
    }
    void StartAdjust(ControlTarget from, ControlTarget to, uint32_t now) {
        mFrom = from;
        mTo = to;
        mCurrent = from;
        mPhase = Phase::Adjust;
        mAdjustSince = now;
        mHigh = mLow = false;
    }
    static ControlTarget Observed(const SensorSnapshot& s, ControlTarget fallback) {
        const auto& o = s.observedOutputs;
        if (!o.valid)
            return fallback;
        return {o.lightOn,
                static_cast<uint8_t>(o.lightOn ? o.brightness : 0),
                o.cct,
                o.rgb,
                o.red,
                o.green,
                o.blue,
                o.fanOn,
                static_cast<uint8_t>(o.fanOn ? o.fanPercent : 0),
                fallback.sensoryScore};
    }
    static bool Arrived(const SensorSnapshot& s, const ControlTarget& t, uint32_t now) {
        const auto& o = s.observedOutputs;
        if (!o.valid)
            return false; // Never claim physical arrival without telemetry.
        return now - s.observedAtMs <= 1000 && o.lightObservationConsistent && o.fanOutputValid && !o.fanBoosting &&
               o.lightOn == t.lightOn &&
               (!t.lightOn ||
                (o.brightness == t.brightnessPercent && o.rgb == t.rgbMode &&
                 (t.rgbMode ? o.red == t.red && o.green == t.green && o.blue == t.blue : o.cct == t.cctMireds))) &&
               o.fanOn == t.fanOn && (!t.fanOn || o.fanPercent == t.fanPercent);
    }
    static ControlTarget Blend(const ControlTarget& a, const ControlTarget& b, uint32_t elapsed, uint32_t duration,
                               const AppConfig& c) {
        const float f = ClampValue(float(elapsed) / duration, 0.0f, 1.0f);
        auto t = b;
        t.brightnessPercent = static_cast<uint8_t>(
            std::lround(a.brightnessPercent + (int(b.brightnessPercent) - a.brightnessPercent) * f));
        t.cctMireds = static_cast<uint16_t>(std::lround(a.cctMireds + (int(b.cctMireds) - a.cctMireds) * f));
        const auto fan = static_cast<uint8_t>(std::lround(a.fanPercent + (int(b.fanPercent) - a.fanPercent) * f));
        // Values below minimum motor duty cannot be held; never call them actual output.
        t.fanPercent = fan < FAN_MIN_DUTY_PERCENT && b.fanPercent == 0 ? 0 : NormalizeFanPercent(fan, c.fanMaxPercent);
        t.lightOn = t.brightnessPercent > 0;
        t.fanOn = t.fanPercent > 0;
        return t;
    }
};
