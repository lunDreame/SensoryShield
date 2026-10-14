// 보드 응답을 받기 전 표시/누락 필드에만 쓰는 시연 기본값.
// 실제 보드가 반환한 저장 설정(실사용 프로필 포함)이 항상 우선한다.
export const demoTimingDefaults = {
  reliefHoldMs: 5000,
  // 실사용 시작 예시: reliefHoldMs: 30 * 60 * 1000,
  recoverConfirmMs: 3000,
  // 실사용 시작 설정도 동일: recoverConfirmMs: 3000 (공학 설정·검증 대상),
  occupancyTimeoutMs: 30 * 1000,
  // 실사용 시작 예시: occupancyTimeoutMs: 15 * 60 * 1000,
} as const;
