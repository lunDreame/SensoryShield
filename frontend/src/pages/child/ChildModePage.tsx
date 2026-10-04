import { useState } from "react";
import { setFan, setLight, setMode } from "../../shared/api/client";
import { formatDurationMinutes } from "../../shared/lib/duration";
import type { StatusResponse } from "../../shared/types/domain";

interface ChildModePageProps {
  status: StatusResponse;
  connectionError: boolean;
}

type ComfortAction = "BRIGHT" | "LOUD" | "COMFORTABLE";

const durations = [
  { minutes: 10, label: "잠깐" },
  { minutes: 30, label: "조금 오래" },
  { minutes: 60, label: "오래" }
] as const;

function roomMessage(status: StatusResponse) {
  if (!status.system.ready) return "기기를 기다리고 있어요";
  if (!status.sensor.occupied) return "방이 쉬고 있어요";
  if (status.sensor.sensoryScore < 2.8) return "지금 방은 편안해요";
  if (status.sensor.sensoryScore < 5.6) return "방에 조금 변화가 있어요";
  return "빛이나 소리가 강해졌어요";
}

export function ChildModePage({ status, connectionError }: ChildModePageProps) {
  const [durationMinutes, setDurationMinutes] = useState<number>(30);
  const [pending, setPending] = useState<ComfortAction | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  async function apply(action: ComfortAction) {
    if (!status.system.ready || pending) return;
    setPending(action);
    setMessage(null);
    setError(null);

    try {
      if (action === "COMFORTABLE") {
        const result = await setMode("AUTO");
        if (!result.ok) throw new Error("rejected");
        setMessage("좋아요. 나에게 맞는 자동 환경으로 돌아갈게요.");
        return;
      }

      const brightness = action === "BRIGHT"
        ? Math.min(status.outputs.brightnessPercent || 32, 32)
        : Math.min(status.outputs.brightnessPercent || 38, 38);
      const lightResult = await setLight({ power: true, brightness, cct: 417 });
      if (!lightResult.ok) throw new Error("rejected");

      if (action === "LOUD") {
        const fanResult = await setFan({
          power: status.outputs.fanOn,
          speed: status.outputs.fanOn ? Math.min(status.outputs.fanPercent, 18) : 0
        });
        if (!fanResult.ok) throw new Error("rejected");
      }

      const modeResult = await setMode("OVERRIDE", durationMinutes);
      if (!modeResult.ok) throw new Error("rejected");
      setMessage(action === "BRIGHT"
        ? `조명을 천천히 낮추고 따뜻하게 바꿀게요. ${formatDurationMinutes(durationMinutes)} 뒤 자동으로 돌아가요.`
        : `조명을 편안하게 바꾸고 팬 소리를 줄일게요. ${formatDurationMinutes(durationMinutes)} 뒤 자동으로 돌아가요.`);
    } catch {
      setError("지금은 기기에 알려주지 못했어요. 연결을 확인하고 다시 눌러주세요.");
    } finally {
      setPending(null);
    }
  }

  return (
    <main className="child-page">
      <section className="child-comfort-card" aria-labelledby="child-room-title">
        <span className="child-kicker">우리 집</span>
        <h1 id="child-room-title">{roomMessage(status)}</h1>
        <p>{status.system.ready ? "불편한 것이 있다면 아래에서 알려주세요." : connectionError ? "USB 연결을 확인해 주세요." : "연결되면 방의 상태를 알려드릴게요."}</p>
        <div className={`child-room-orb${status.system.ready && status.sensor.sensoryScore >= 5.6 ? " is-strong" : ""}`} aria-hidden="true">
          <img src="/illustrations/child-at-home.png" alt="" />
        </div>
      </section>

      <section className="child-choice-section" aria-labelledby="child-choice-title">
        <div className="child-section-heading">
          <div>
            <span className="child-kicker">지금 느끼는 것</span>
            <h2 id="child-choice-title">어떻게 느껴지나요?</h2>
          </div>
          <div className="child-duration" aria-label="편안한 환경 유지 시간">
            <span>얼마 동안 편하게 할까요?</span>
            <div>
              {durations.map((duration) => (
                <button className={durationMinutes === duration.minutes ? "active" : ""} type="button"
                  aria-pressed={durationMinutes === duration.minutes} onClick={() => setDurationMinutes(duration.minutes)} key={duration.minutes}>
                  <span className="duration-clock" aria-hidden="true"><i /></span>
                  <span><b>{duration.label}</b><small>{duration.minutes === 60 ? "1시간" : `${duration.minutes}분`}</small></span>
                  <span className="duration-check" aria-hidden="true">✓</span>
                </button>
              ))}
            </div>
          </div>
        </div>

        <div className="child-choice-grid">
          <button className="child-choice child-choice-light" type="button" disabled={!status.system.ready || pending !== null} onClick={() => void apply("BRIGHT")}>
            <span className="child-choice-icon" aria-hidden="true"><img src="/illustrations/soft-light.png" alt="" /></span>
            <span><b>{pending === "BRIGHT" ? "바꾸는 중이에요" : "너무 밝아요"}</b><small>빛을 낮추고 따뜻하게</small></span>
          </button>
          <button className="child-choice child-choice-sound" type="button" disabled={!status.system.ready || pending !== null} onClick={() => void apply("LOUD")}>
            <span className="child-choice-icon" aria-hidden="true"><img src="/illustrations/quiet-sound.png" alt="" /></span>
            <span><b>{pending === "LOUD" ? "바꾸는 중이에요" : "너무 시끄러워요"}</b><small>빛과 팬 소리를 편안하게</small></span>
          </button>
          <button className="child-choice child-choice-good" type="button" disabled={!status.system.ready || pending !== null} onClick={() => void apply("COMFORTABLE")}>
            <span className="child-choice-icon" aria-hidden="true"><img src="/illustrations/comfortable-home.png" alt="" /></span>
            <span><b>{pending === "COMFORTABLE" ? "돌아가는 중이에요" : "지금 좋아요"}</b><small>나에게 맞는 자동 환경으로</small></span>
          </button>
        </div>

        {message ? <div className="child-feedback" role="status"><span>✓</span>{message}</div> : null}
        {error ? <div className="child-feedback is-error" role="alert"><span>!</span>{error}</div> : null}
      </section>
    </main>
  );
}
