import { useMemo, useState } from "react";
import { saveProfile, setMode } from "../../shared/api/client";
import type { AppConfig } from "../../shared/types/domain";

type Level = "low" | "medium" | "high";
interface ProfileOnboardingProps { initialConfig: AppConfig; onComplete: (config: AppConfig) => void; }
const levels = [
  { value: "low" as Level, label: "거의 불편하지 않아요", description: "같은 변화에도 조절하는 정도가 작아요." },
  { value: "medium" as Level, label: "가끔 불편해요", description: "변화에 반응하는 정도를 중간으로 정해요." },
  { value: "high" as Level, label: "쉽게 불편해져요", description: "작은 변화에도 더 크게 조절해요." }
];
const tones = [
  { cct: 250, label: "선명한 흰빛", swatch: "clear" },
  { cct: 333, label: "중간 빛", swatch: "balanced" },
  { cct: 454, label: "따뜻한 빛", swatch: "calm" }
];
function sensitivity(weight: number): Level { return weight >= .7 ? "high" : weight <= .35 ? "low" : "medium"; }
export function ProfileOnboarding({ initialConfig, onComplete }: ProfileOnboardingProps) {
  const [step, setStep] = useState(0);
  const [lightSensitivity, setLightSensitivity] = useState<Level>(sensitivity(initialConfig.lightWeight));
  const [soundSensitivity, setSoundSensitivity] = useState<Level>(sensitivity(initialConfig.soundWeight));
  const [cct, setCct] = useState(initialConfig.preferredCCTMireds);
  const [pending, setPending] = useState(false);
  const [saved, setSaved] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const result = useMemo<AppConfig>(() => ({ ...initialConfig,
    lightWeight: { low: .3, medium: .55, high: .8 }[lightSensitivity],
    soundWeight: { low: .25, medium: .45, high: .75 }[soundSensitivity],
    preferredCCTMireds: cct, profileConfigured: true
  }), [initialConfig, lightSensitivity, soundSensitivity, cct]);
  async function finish() {
    setPending(true); setError(null);
    let committed = saved;
    try {
      if (!committed) {
        if (!(await saveProfile(result)).ok) throw new Error();
        committed = true; setSaved(true);
      }
      if (!(await setMode("AUTO")).ok) throw new Error();
      onComplete(result);
    } catch { setError(committed ? "설정은 저장됐어요. 자동 시작을 다시 눌러주세요." : "설정을 저장하지 못했어요. 연결을 확인한 뒤 다시 시도해 주세요."); }
    finally { setPending(false); }
  }
  const questions = [
    { title: "갑자기 밝아질 때 얼마나 불편한가요?", description: "평소보다 밝아졌을 때 조명과 팬을 함께 완화하는 반응 정도를 정해요.", value: lightSensitivity, select: setLightSensitivity },
    { title: "소리가 커질 때 얼마나 불편한가요?", description: "평소보다 소리가 커졌을 때 조명과 팬이 반응하는 정도를 정해요.", value: soundSensitivity, select: setSoundSensitivity }
  ];
  const selectedPreset = tones.reduce((closest, tone) => Math.abs(tone.cct - cct) < Math.abs(closest.cct - cct) ? tone : closest, tones[0]);
  return <main className="onboarding-shell"><section className="onboarding-card" aria-labelledby="onboarding-title">
    <div className="onboarding-brand"><img src="/brand/sensoryshield-logo.png" alt="SensoryShield" /></div>
    <div className="onboarding-progress-row"><div className="onboarding-progress" aria-label={`${step + 1}단계, 전체 3단계`}>
      {[0, 1, 2].map(index => <i className={index <= step ? "active" : ""} key={index} />)}
    </div><span>{step + 1} / 3</span></div>
    <span className="onboarding-kicker">나에게 맞는 자동 조절</span>
    {step < 2 ? <>
      <h1 id="onboarding-title">{questions[step].title}</h1><p className="onboarding-description">{questions[step].description}</p>
      <div className="choice-list">{levels.map(option => <button className={`choice-card${questions[step].value === option.value ? " selected" : ""}`}
        type="button" disabled={pending || saved} aria-pressed={questions[step].value === option.value} onClick={() => questions[step].select(option.value)} key={option.value}>
        <span className="choice-radio" /><span><b>{option.label}</b><small>{option.description}</small></span>
      </button>)}</div>
    </> : <>
      <h1 id="onboarding-title">자극이 커지면 어떤 빛으로 바꿔줄까요?</h1>
      <p className="onboarding-description">조명을 낮추면서 아이가 편안해하는 빛으로 바꿔줘요. 보호자가 아이의 반응을 참고해 선택해 주세요. 나중에 설정에서 변경할 수 있어요.</p>
      <div className="light-choice-grid">{tones.map(tone => <button type="button" className={`light-choice${selectedPreset.cct === tone.cct ? " selected" : ""}`}
        key={tone.cct} disabled={pending || saved} aria-pressed={selectedPreset.cct === tone.cct}
        onClick={() => setCct(Math.max(initialConfig.minCCTMireds, Math.min(initialConfig.maxCCTMireds, tone.cct)))}>
        <span className={`light-swatch ${tone.swatch}`} /><span className="light-choice-copy"><b>{tone.label}</b></span>
      </button>)}</div>
      <p className="experience-note">선택한 빛은 설정된 색온도 범위 안에서 적용돼요.</p>
      <p className="experience-note">현재 선택한 색온도: {Math.round(1000000 / cct).toLocaleString()} K</p>
      <p className="profile-setting-note">평소 환경을 자동으로 학습하고, 빛과 소리 변화에 맞춰 조명과 팬을 조절해요.</p>
    </>}
    {error ? <div className="notice notice-error onboarding-error" role="alert">{error}</div> : null}
    <div className="onboarding-actions">
      {step > 0 ? <button className="button button-neutral" type="button" disabled={pending || saved} onClick={() => { setError(null); setStep(step - 1); }}>이전</button> : null}
      {step < 2 ? <button className="button button-primary" type="button" onClick={() => { setError(null); setStep(step + 1); }}>다음</button> :
        <button className="button button-primary" type="button" disabled={pending} onClick={() => void finish()}>{pending ? "저장 중" : saved ? "자동 시작 다시 시도" : "저장하고 자동 시작"}</button>}
    </div>
  </section></main>;
}
