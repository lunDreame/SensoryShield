import { useRef, useState } from "react";
import { setMode } from "../../shared/api/client";
import { formatRemainingSeconds } from "../../shared/lib/duration";
import type { ControlMode } from "../../shared/types/domain";

const modes: ControlMode[] = ["AUTO", "MANUAL", "OVERRIDE", "SAFE"];
const labels: Record<ControlMode, string> = {
  AUTO: "자동",
  MANUAL: "수동",
  OVERRIDE: "잠시 사용",
  SAFE: "안전"
};

const descriptions: Record<ControlMode, string> = {
  AUTO: "처음 설정한 개인 기준과 현재 센서 상태에 맞춰 조명과 팬을 자동으로 조절합니다.",
  MANUAL: "직접 설정한 조명과 팬을 유지합니다. 자동으로 돌아가려면 ‘자동’을 선택하세요.",
  OVERRIDE: "저장된 설정을 15분간 사용한 뒤 개인 맞춤 자동 모드로 돌아갑니다. 조명이나 팬을 다시 조절하면 수동 모드로 바뀝니다.",
  SAFE: "조명과 팬을 정지하는 모드입니다. 장치 상태를 확인한 뒤 자동 또는 수동을 선택하세요."
};

interface ModeSwitchProps {
  mode: ControlMode;
  available: boolean;
  overrideRemainingSeconds: number;
}

export function ModeSwitch({ mode, available, overrideRemainingSeconds }: ModeSwitchProps) {
  const [pending, setPending] = useState<ControlMode | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);
  const requestInFlight = useRef(false);

  async function changeMode(next: ControlMode) {
    if (requestInFlight.current || !available || next === "SAFE" || next === mode) return;
    requestInFlight.current = true;
    setPending(next);
    setError(null);
    setMessage(null);
    try {
      const result = await setMode(next);
      if (!result.ok) throw new Error("rejected");
      setMessage(`${labels[next]} 모드로 변경을 요청했어요. 수신된 작동 방식을 확인해 주세요.`);
    } catch {
      setError("기기와 연결할 수 없어 제어 모드를 변경하지 못했어요.");
    } finally {
      requestInFlight.current = false;
      setPending(null);
    }
  }

  return (
    <div>
    <div className="segmented" role="group" aria-label="제어 모드" aria-describedby="mode-description">
      {modes.map((item) => (
        <button
          className={`mode-option mode-${item.toLowerCase()}${item === mode ? " active" : ""}`}
          disabled={pending !== null || !available || item === "SAFE" || item === mode}
          key={item}
          type="button"
          aria-pressed={item === mode}
          onClick={() => void changeMode(item)}
        >
          <span className="mode-dot" aria-hidden="true" />{pending === item ? "변경 중" : labels[item]}
        </button>
      ))}
    </div>
    <div id="mode-description" className={`mode-description mode-${mode.toLowerCase()}`}>
      <b>{available ? `현재 ${labels[mode]} 모드` : "작동 방식 확인 대기"}</b>
      <p>{available ? mode === "OVERRIDE" ? `현재 설정을 약 ${formatRemainingSeconds(overrideRemainingSeconds)} 더 유지한 뒤 개인 맞춤 자동으로 돌아갑니다.` : descriptions[mode] : "연결 후 작동 방식을 확인하고 변경할 수 있어요."}</p>
    </div>
    {available && (mode === "MANUAL" || mode === "OVERRIDE") ? <button className="button button-weak mode-return" type="button" disabled={pending !== null} onClick={() => void changeMode("AUTO")}>개인 맞춤 자동으로 돌아가기</button> : null}
    {message && available ? <p className="panel-subtitle" role="status" style={{ marginTop: 8 }}>{message}</p> : null}
    {error ? <p className="inline-error" role="alert">{error}</p> : null}
    </div>
  );
}
