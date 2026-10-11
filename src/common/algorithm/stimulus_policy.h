#pragma once
#include "definition.h"

// Shared engineering thresholds. Confirmation times describe controller input,
// not a child's discomfort or calming time.
namespace StimulusPolicy {
inline constexpr float LearnScore = 0.7f; // Inclusive; (0.7, 1.4) is observation-only for increases.
inline constexpr float EnterScore = 1.4f;
inline constexpr float RecoverScore = 0.7f;
inline constexpr uint32_t EnterMs = 750;
inline constexpr uint8_t OutputStepPercent = 2;
inline constexpr uint32_t RecoveryStepMs = 500;
inline constexpr uint32_t RecoverMs = AUTO_RECOVER_CONFIRM_MS;
}
