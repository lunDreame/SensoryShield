#pragma once
#include "definition.h"

// Shared engineering thresholds. Confirmation times describe controller input,
// not a child's discomfort or calming time.
// Demo confirms sustained input for 3 seconds. Everyday 750 ms remains an
// engineering starting value, not a clinically validated threshold.
namespace StimulusPolicy {
inline constexpr float LearnScore = 0.7f; // Inclusive; (0.7, 1.4) is observation-only for increases.
inline constexpr float EnterScore = 1.4f;
inline constexpr float RecoverScore = 0.7f;
#if SENSORYSHIELD_DEMO_DEFAULTS
inline constexpr uint32_t EnterMs = 3000;
// Everyday controller: inline constexpr uint32_t EnterMs = 750;
#else
inline constexpr uint32_t EnterMs = 750;
#endif
inline constexpr uint8_t OutputStepPercent = 2;
inline constexpr uint32_t RecoveryStepMs = 500;
inline constexpr uint32_t RecoverMs = AUTO_RECOVER_CONFIRM_MS;
}
