export function describeLightLevel(lux: number) {
  if (lux < 50) return "매우 어두운 편이에요";
  if (lux < 150) return "은은한 밝기예요";
  if (lux < 300) return "편안한 실내 밝기예요";
  if (lux < 500) return "밝은 편이에요";
  return "매우 밝은 편이에요";
}

export function describeKelvin(kelvin: number) {
  if (kelvin < 2500) return "아늑한 전구빛";
  if (kelvin < 3000) return "편안한 따뜻한 빛";
  if (kelvin < 3500) return "자연스러운 흰빛";
  return "또렷하고 선명한 빛";
}

export function describeSensitivity(percent: number) {
  if (percent < 35) return "천천히 반응";
  if (percent < 70) return "균형 있게 반응";
  return "민감하게 반응";
}
