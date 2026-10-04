export function formatDurationMinutes(totalMinutes: number) {
  const minutes = Math.max(1, Math.ceil(totalMinutes));
  const hours = Math.floor(minutes / 60);
  const remainder = minutes % 60;
  if (hours === 0) return `${minutes}분`;
  if (remainder === 0) return `${hours}시간`;
  return `${hours}시간 ${remainder}분`;
}

export function formatRemainingSeconds(seconds: number) {
  return formatDurationMinutes(seconds / 60);
}
