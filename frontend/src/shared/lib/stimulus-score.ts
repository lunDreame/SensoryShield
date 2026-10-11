// Matches STIMULUS_MAX_SCORE in src/definition.h. Display bands retain
// the existing 35% / 70% proportions; they are not clinical thresholds.
export const STIMULUS_MAX_SCORE = 4;
export const STIMULUS_CHANGE_THRESHOLD = STIMULUS_MAX_SCORE * 0.35;
export const STIMULUS_STRONG_THRESHOLD = STIMULUS_MAX_SCORE * 0.7;

export function displayedStimulusScore(value: number): number | null {
  return Number.isFinite(value) ? Math.min(STIMULUS_MAX_SCORE, Math.max(0, value)) : null;
}
