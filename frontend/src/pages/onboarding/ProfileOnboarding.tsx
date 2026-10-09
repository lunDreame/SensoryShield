import { useMemo, useState } from "react";
import { saveProfile } from "../../shared/api/client";
import type { AppConfig } from "../../shared/types/domain";

type Level = "low" | "medium" | "high";
type LightPreference = "clear" | "balanced" | "calm";

interface ProfileOnboardingProps {
  initialConfig: AppConfig;
  onComplete: (config: AppConfig) => void;
}

const levels: Array<{ value: Level; label: string; description: string }> = [
  { value: "low", label: "거의 불편하지 않아요", description: "변화가 커도 조명이 천천히 반응해요." },
  { value: "medium", label: "가끔 불편해요", description: "일상적인 변화와 강한 자극을 균형 있게 구분해요." },
  { value: "high", label: "쉽게 불편해져요", description: "작은 변화도 더 빠르게 조명에 반영해요." }
];

const lightPreferences: Array<{ value: LightPreference; label: string; description: string; swatch: string }> = [
  { value: "clear", label: "밝고 선명하게", description: "밝은 빛과 또렷한 흰빛을 선호해요.", swatch: "clear" },
  { value: "balanced", label: "편안하고 균형 있게", description: "밝기와 따뜻함을 중간 범위로 맞춰요.", swatch: "balanced" },
  { value: "calm", label: "은은하고 따뜻하게", description: "낮은 밝기와 따뜻한 빛을 선호해요.", swatch: "calm" }
];

const weightByLevel: Record<Level, number> = { low: 0.3, medium: 0.55, high: 0.8 };
const soundWeightByLevel: Record<Level, number> = { low: 0.25, medium: 0.45, high: 0.75 };
const lightRangeByPreference: Record<LightPreference, Pick<AppConfig, "minBrightness" | "maxBrightness" | "minCCTMireds" | "maxCCTMireds">> = {
  clear: { minBrightness: 15, maxBrightness: 90, minCCTMireds: 250, maxCCTMireds: 370 },
  balanced: { minBrightness: 10, maxBrightness: 80, minCCTMireds: 286, maxCCTMireds: 417 },
  calm: { minBrightness: 5, maxBrightness: 65, minCCTMireds: 333, maxCCTMireds: 454 }
};

export function ProfileOnboarding({ initialConfig, onComplete }: ProfileOnboardingProps) {
  const [step, setStep] = useState(0);
  const [lightSensitivity, setLightSensitivity] = useState<Level>("medium");
  const [soundSensitivity, setSoundSensitivity] = useState<Level>("medium");
  const [lightPreference, setLightPreference] = useState<LightPreference>("balanced");
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const result = useMemo<AppConfig>(() => ({
    ...initialConfig,
    lightWeight: weightByLevel[lightSensitivity],
    soundWeight: soundWeightByLevel[soundSensitivity],
    ...lightRangeByPreference[lightPreference],
    profileConfigured: true
  }), [initialConfig, lightPreference, lightSensitivity, soundSensitivity]);

  async function finish(config = result) {
    setPending(true);
    setError(null);
    try {
      await saveProfile(config);
      onComplete(config);
    } catch {
      setError("기기에 설정을 저장하지 못했어요. 기기 연결을 확인한 뒤 다시 시도해 주세요.");
    } finally {
      setPending(false);
    }
  }

  const questions = [
    {
      eyebrow: "1 · 3",
      title: "밝은 빛이 얼마나 불편한가요?",
      description: "선택한 정도에 따라 주변이 갑자기 밝아졌을 때 조명이 반응하는 크기를 정해요.",
      options: levels,
      value: lightSensitivity,
      select: setLightSensitivity
    },
    {
      eyebrow: "2 · 3",
      title: "큰 소리가 얼마나 불편한가요?",
      description: "소음이 평소보다 커졌을 때 조명을 낮추고 따뜻하게 바꾸는 정도를 정해요.",
      options: levels,
      value: soundSensitivity,
      select: setSoundSensitivity
    }
  ];

  return (
    <main className="onboarding-shell">
      <section className="onboarding-card" aria-labelledby="onboarding-title">
        <div className="onboarding-brand">
          <img src="/brand/sensoryshield-logo.png" alt="SensoryShield" />
        </div>
        <div className="onboarding-progress-row">
          <div className="onboarding-progress" aria-label={`${step + 1}단계, 전체 3단계`}>
            {[0, 1, 2].map((index) => <i className={index <= step ? "active" : ""} key={index} />)}
          </div>
          <span>{step + 1} / 3</span>
        </div>

        {step < 2 ? (
          <>
            <span className="onboarding-kicker">나에게 맞는 환경 찾기</span>
            <h1 id="onboarding-title">{questions[step].title}</h1>
            <p className="onboarding-description">{questions[step].description}</p>
            <div className="choice-list">
              {questions[step].options.map((option) => (
                <button
                  className={`choice-card${questions[step].value === option.value ? " selected" : ""}`}
                  type="button"
                  aria-pressed={questions[step].value === option.value}
                  onClick={() => questions[step].select(option.value)}
                  key={option.value}
                >
                  <span className="choice-radio" />
                  <span><b>{option.label}</b><small>{option.description}</small></span>
                </button>
              ))}
            </div>
          </>
        ) : (
          <>
            <span className="onboarding-kicker">나에게 맞는 환경 찾기</span>
            <h1 id="onboarding-title">어떤 조명이 가장 편안한가요?</h1>
            <p className="onboarding-description">AUTO 모드가 사용할 밝기와 빛의 따뜻함 범위를 정해요. 나중에 설정에서 바꿀 수 있어요.</p>
            <div className="light-choice-grid">
              {lightPreferences.map((option) => (
                <button
                  className={`light-choice${lightPreference === option.value ? " selected" : ""}`}
                  type="button"
                  aria-pressed={lightPreference === option.value}
                  onClick={() => setLightPreference(option.value)}
                  key={option.value}
                >
                  <span className={`light-swatch ${option.swatch}`} />
                  <span className="light-choice-copy">
                    <b>{option.label}</b>
                    <small>{option.description}</small>
                  </span>
                  <span className="choice-check" aria-hidden="true">✓</span>
                </button>
              ))}
            </div>
          </>
        )}

        {error ? <div className="notice notice-error onboarding-error" role="alert"><i className="notice-dot" />{error}</div> : null}
        <div className="onboarding-actions">
          {step > 0 ? <button className="button button-neutral" type="button" disabled={pending} onClick={() => setStep(step - 1)}>이전</button> : (
            <button className="button button-neutral" type="button" disabled={pending} onClick={() => void finish({ ...initialConfig, profileConfigured: true })}>기본 설정으로 시작</button>
          )}
          {step < 2 ? (
            <button className="button button-primary" type="button" onClick={() => setStep(step + 1)}>다음</button>
          ) : (
            <button className="button button-primary" type="button" disabled={pending} onClick={() => void finish()}>{pending ? "저장 중" : "설정 완료"}</button>
          )}
        </div>
      </section>
    </main>
  );
}
