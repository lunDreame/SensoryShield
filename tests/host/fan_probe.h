#pragma once
#include "common/algorithm/sensory_relief.h"
#include "common/algorithm/stimulus_score.h"
class FanProbe {
    SensoryReliefController controller;
  public:
    static constexpr uint32_t EnterQuietMs = StimulusPolicy::EnterMs;
    bool IsNormal() const { return controller.IsNormal(); }
    uint8_t Evaluate(const SensorSnapshot& s, AppConfig c, const StimulusScore& score, uint32_t now) {
        c.preferredBrightness = c.minBrightness = 0;
        return controller.Evaluate(s,c,score.combined,now).fanPercent;
    }
};
