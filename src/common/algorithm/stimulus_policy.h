#pragma once
#include "definition.h"

// Shared engineering thresholds. Confirmation times describe controller input,
// not a child's discomfort or calming time.
// 점수 문턱, 750ms 진입 확인, 출력 변화 폭/주기는 두 프로필 공통이다.
// 임상 확정값이나 시연만을 위해 단축한 값이 아니다.
// 복귀 확인 시간은 definition.h를 따르며 시연·실사용 모두 기본 3초이다.
namespace StimulusPolicy {
inline constexpr float LearnScore = 0.7f; // Inclusive; (0.7, 1.4) is observation-only for increases.
inline constexpr float EnterScore = 1.4f;
inline constexpr float RecoverScore = 0.7f;
inline constexpr uint32_t EnterMs = 750;
inline constexpr uint8_t OutputStepPercent = 2;
inline constexpr uint32_t RecoveryStepMs = 500;
inline constexpr uint32_t RecoverMs = AUTO_RECOVER_CONFIRM_MS;
}
