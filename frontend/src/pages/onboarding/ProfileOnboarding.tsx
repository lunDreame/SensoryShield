import { useMemo, useState } from "react";
import { saveProfile, saveEnvironmentBaseline } from "../../shared/api/client";
import { ENVIRONMENT_SAMPLE_COUNT, measureEnvironment, type EnvironmentMeasurement } from "../../features/measure-environment/measureEnvironment";
import type { AppConfig } from "../../shared/types/domain";

type Level = "low" | "medium" | "high";
type LightPreference = "clear" | "balanced" | "calm";

interface ProfileOnboardingProps {
  initialConfig: AppConfig;
  measurementOnly?: boolean;
  onCancel?: () => void;
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

export function ProfileOnboarding({ initialConfig, onComplete, measurementOnly = false, onCancel }: ProfileOnboardingProps) {
  const [step, setStep] = useState(measurementOnly ? 3 : 0);
  const [lightSensitivity, setLightSensitivity] = useState<Level>("medium");
  const [soundSensitivity, setSoundSensitivity] = useState<Level>("medium");
  const [lightPreference, setLightPreference] = useState<LightPreference>("balanced");
  const [useDefaults, setUseDefaults] = useState(measurementOnly);
  const [measurement, setMeasurement] = useState<EnvironmentMeasurement | null>(null);
  const [progress, setProgress] = useState(0);
  const [measuring, setMeasuring] = useState(false);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const result = useMemo<AppConfig>(() => ({
    ...initialConfig,
    lightWeight: weightByLevel[lightSensitivity],
    soundWeight: soundWeightByLevel[soundSensitivity],
    ...lightRangeByPreference[lightPreference],
    profileConfigured: true
  }), [initialConfig, lightPreference, lightSensitivity, soundSensitivity]);

  async function measure() {
    setMeasuring(true);
    setMeasurement(null);
    setProgress(0);
    setError(null);
    try {
      setMeasurement(await measureEnvironment(setProgress));
    } catch (error) {
      setError(error instanceof Error && /[가-힣]/.test(error.message) ? error.message : "측정하지 못했어요. 기기 연결을 확인하고 다시 시도해 주세요.");
    } finally {
      setMeasuring(false);
    }
  }

  async function finish(config = useDefaults ? { ...initialConfig, profileConfigured: true } : result) {
    setPending(true);
    setError(null);
    try {
      if (!measurement) throw new Error("missing measurement");
      if (!(await saveEnvironmentBaseline(measurement)).ok) throw new Error("baseline rejected");
      if (!measurementOnly && !(await saveProfile(config)).ok) throw new Error("rejected");
      onComplete(config);
    } catch {
      setError(measurementOnly ? "측정 기준의 저장 완료를 확인하지 못했어요. 기기 연결을 확인한 뒤 다시 저장해 주세요." : "측정 기준 또는 개인 설정을 저장하지 못했어요. 기기 연결을 확인한 뒤 설정 완료를 다시 눌러 주세요. 기준만 먼저 저장된 경우에는 같은 결과로 다시 저장할 수 있어요.");
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
          <div className="onboarding-progress" aria-label={`${step + 1}단계, 전체 4단계`}>
            {[0, 1, 2, 3].map((index) => <i className={index <= step ? "active" : ""} key={index} />)}
          </div>
          <span>{step + 1} / 4</span>
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
        ) : step === 2 ? (
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
        ) : (
          <>
            <span className="onboarding-kicker">우리 집의 평소 환경 확인</span>
            <h1 id="onboarding-title">평소 환경을 측정해 주세요</h1>
            <p className="onboarding-description">실제 사용할 자리에서 평소의 밝기와 주변 소리를 확인해요. 아래 준비를 마친 뒤 측정을 시작해 주세요.</p>
            <div className="measurement-guide">
              <h2>측정 전 준비</h2>
              <ol>
                <li><b>기기와 센서를 실제 사용할 위치에 놓아 주세요.</b> 조도 센서와 마이크를 손이나 물건으로 가리지 말고, 위치와 방향을 정해 주세요.</li>
                <li><b>평소 사용하는 조명과 주변 소리를 유지해 주세요.</b> 커튼과 실내 조명은 평소 상태로 두고, TV나 음악은 일상적으로 사용하는 정도로 맞춰 주세요. 공사 소리처럼 일시적인 큰 소리가 있다면 잦아든 뒤 측정해 주세요.</li>
                <li><b>기기의 조명과 팬은 측정 직전 상태로 유지됩니다.</b> 측정 중 자동 조절을 잠시 멈추며, 끝나면 이전 작동 방식으로 돌아가요.</li>
              </ol>
              <h2>측정 중에는 이렇게 해 주세요</h2>
              <ul>
                <li>기기·센서·팬을 옮기거나 센서를 가리지 마세요.</li>
                <li>손전등을 비추거나 박수를 치는 등 테스트 자극을 주지 마세요.</li>
                <li>조명·커튼·팬 속도를 바꾸거나 웹·SmartThings에서 기기를 조작하지 마세요.</li>
                <li>주변 환경이 갑자기 달라졌다면 안정된 뒤 다시 측정해 주세요.</li>
              </ul>
              <p className="measurement-note">측정은 약 10초 이상 걸리며 연결 상태에 따라 더 길어질 수 있어요. 이 결과는 평소 환경을 비교하기 위한 자료이며, 밝기나 소음이 적절하거나 편안한 수준인지 판정하는 결과는 아닙니다.</p>
            </div>
            <div className="measurement-status" role="status" aria-live="polite">
              {measuring ? <><b>평소 환경을 측정하고 있어요</b><progress value={progress} max={ENVIRONMENT_SAMPLE_COUNT} /><span>측정 중에는 현재 환경을 유지하고, 화면을 닫거나 새로고침하지 마세요.</span></> : measurement ? <><b>측정이 완료됐어요</b><span>평소 밝기: {measurement.luxMedian.toFixed(1)} lx</span><span>평소 소리: {(measurement.soundMedian * 100).toFixed(1)}% · 마이크 상대 입력, dB 아님</span><span>설정 완료를 누르면 이 결과가 기기에 저장되고 자동 조절의 비교 기준으로 사용돼요. 전원을 다시 켜도 유지되며, 환경이 바뀌면 다시 측정해 주세요.</span></> : <span>준비가 끝나면 아래 버튼을 눌러 주세요.</span>}
            </div>
            <button className={`button measurement-start${measuring ? " is-measuring" : ""}`} type="button" disabled={measuring || pending} onClick={() => void measure()}>
              <span className="measurement-start-icon" aria-hidden="true">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round">
                  <path d="M8 3H5a2 2 0 0 0-2 2v3m13-5h3a2 2 0 0 1 2 2v3M3 16v3a2 2 0 0 0 2 2h3m8 0h3a2 2 0 0 0 2-2v-3" />
                  <path d="M7 12h2l2-4 2 8 2-4h2" />
                </svg>
              </span>
              <span className="measurement-start-copy"><b>{measuring ? "평소 환경 측정 중" : measurement || error ? "평소 환경 다시 측정" : "평소 환경 측정 시작"}</b><small>{measuring ? "잠시만 기다려 주세요" : "준비를 마쳤다면 눌러 주세요"}</small></span>
              <span className="measurement-start-arrow" aria-hidden="true">{measuring ? "···" : "→"}</span>
            </button>
          </>
        )}

        {error ? <div className="notice notice-error onboarding-error" role="alert"><i className="notice-dot" />{error}</div> : null}
        <div className="onboarding-actions">
          {step > 0 ? <button className="button button-neutral" type="button" disabled={pending || measuring} onClick={() => { if (measurementOnly) { onCancel?.(); return; } setError(null); setStep(useDefaults ? 0 : step - 1); setUseDefaults(false); setMeasurement(null); }}>{measurementOnly ? "취소" : "이전"}</button> : (
            <button className="button button-neutral" type="button" disabled={pending} onClick={() => { setUseDefaults(true); setStep(3); }}>기본 설정으로 시작</button>
          )}
          {step < 3 ? (
            <button className="button button-primary" type="button" onClick={() => { setError(null); setStep(step + 1); }}>다음</button>
          ) : (
            <button className="button button-primary" type="button" disabled={pending || measuring || !measurement} onClick={() => void finish()}>{pending ? "저장 중" : "설정 완료"}</button>
          )}
        </div>
      </section>
    </main>
  );
}
