import { displayedStimulusScore, STIMULUS_CHANGE_THRESHOLD, STIMULUS_STRONG_THRESHOLD } from "../../shared/lib/stimulus-score";
import { useRef, useState } from "react";
import { requestComfort } from "../../shared/api/client";
import { applyChildComfort, type ComfortAction } from "../../shared/lib/child-control";
import type { StatusResponse } from "../../shared/types/domain";

interface ChildModePageProps {
  status: StatusResponse;
  connectionError: boolean;
}

const actions: Array<{ action: ComfortAction; label: string; description: string; icon: string; style: string }> = [
  { action: "BRIGHT", label: "너무 밝아요", description: "조명을 서서히 낮춰요", icon: "feedback-too-bright", style: "light" },
  { action: "DARK", label: "너무 어두워요", description: "조명을 조금 밝혀요", icon: "feedback-too-dark", style: "dark" },
  { action: "LOUD", label: "너무 시끄러워요", description: "조명과 팬 세기를 낮춰요", icon: "feedback-too-loud", style: "sound" },
  { action: "QUIET", label: "너무 조용해요", description: "팬을 켜거나 조금 높여요", icon: "feedback-too-quiet", style: "quiet" }
];

function roomMessage(status: StatusResponse) {
  if (!status.system.ready) return "기기를 기다리고 있어요";
  if (status.system.mode === "SAFE") return "보호자의 확인이 필요해요";
  if (status.sensor.baselineReady !== true) return "평소 환경을 익히며 빛과 소리 변화를 살펴봐요";
  const score = displayedStimulusScore(status.sensor.sensoryScore);
  if (score === null) return "방의 상태를 확인하고 있어요";
  if (!status.sensor.occupied) return "방이 쉬고 있어요";
  if (score < STIMULUS_CHANGE_THRESHOLD) return "지금 방의 변화가 작아요";
  if (score < STIMULUS_STRONG_THRESHOLD) return "방에 조금 변화가 있어요";
  return "빛이나 소리가 강해졌어요";
}

export function ChildModePage({ status, connectionError }: ChildModePageProps) {
  const activeRequest = useRef(false);
  const [pending, setPending] = useState<ComfortAction | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  async function apply(action: ComfortAction) {
    if (!status.system.ready || status.system.mode === "SAFE" || connectionError || activeRequest.current) return;
    activeRequest.current = true;
    setPending(action);
    setMessage(null);
    setError(null);
    try {
      setMessage(await applyChildComfort(action, { requestComfort }));
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "기기에 알려주지 못했어요. 연결을 확인해 주세요.");
    } finally {
      activeRequest.current = false;
      setPending(null);
    }
  }

  return (
    <main className="child-page">
      <section className="child-comfort-card" aria-labelledby="child-room-title">
        <span className="child-kicker">우리 집</span>
        <h1 id="child-room-title">{connectionError ? "기기 연결을 확인해 주세요" : roomMessage(status)}</h1>
        <p>{connectionError ? "연결되면 다시 알려줄 수 있어요." : status.system.ready ? status.system.mode === "SAFE" ? "보호자에게 기기 상태를 확인해 달라고 해주세요." : status.sensor.baselineReady === false ? "평소 환경을 익히며 빛과 소리 변화를 살펴봐요. 불편하면 아래에서 알려주세요." : "불편하면 아래에서 알려주세요." : "연결되면 방의 상태를 알려드릴게요."}</p>
        <div className={`child-room-orb${status.system.ready && status.sensor.baselineReady === true && (displayedStimulusScore(status.sensor.sensoryScore) ?? 0) >= STIMULUS_STRONG_THRESHOLD ? " is-strong" : ""}`} aria-hidden="true">
          <img src="/illustrations/child-at-home.png" alt="" />
        </div>
      </section>

      <section className="child-choice-section" aria-labelledby="child-choice-title">
        <div className="child-section-heading">
          <div>
            <span className="child-kicker">지금 느끼는 것</span>
            <h2 id="child-choice-title">어떻게 느껴지나요?</h2>
          </div>

        </div>

        <div className="child-choice-grid" aria-busy={pending !== null}>
          {actions.map(option => <button className={`child-choice child-choice-${option.style}`} type="button"
            disabled={!status.system.ready || status.system.mode === "SAFE" || connectionError || pending !== null} onClick={() => void apply(option.action)} key={option.action}>
            <span className="child-choice-icon" aria-hidden="true"><img src={`/illustrations/${option.icon}.png`} alt="" /></span>
            <span><b>{pending === option.action ? "바꾸는 중이에요" : option.label}</b><small>{option.description}</small></span>
          </button>)}
        </div>

        {message ? <div className="child-feedback" role="status"><span>✓</span>{message}</div> : null}
        {error ? <div className="child-feedback is-error" role="alert"><span>!</span>{error}</div> : null}
      </section>
    </main>
  );
}
