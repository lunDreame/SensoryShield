import { useRef, useState } from "react";
import { setMode } from "../../shared/api/client";
import type { ControlMode } from "../../shared/types/domain";

const modes: ControlMode[] = ["AUTO", "MANUAL", "OVERRIDE", "SAFE"];
const labels: Record<ControlMode, string> = {
  AUTO: "자동",
  MANUAL: "수동",
  OVERRIDE: "잠시 사용",
  SAFE: "안전"
};

const descriptions: Record<ControlMode, string> = {
  AUTO: "센서 상태에 따라 조명과 팬을 자동으로 조절합니다.",
  MANUAL: "직접 설정한 조명과 팬을 유지합니다. 자동으로 돌아가려면 ‘자동’을 선택하세요.",
  OVERRIDE: "저장된 수동 설정을 사용하고 15분 뒤 자동으로 돌아갑니다. 조명이나 팬을 직접 조절하면 수동 모드로 바뀝니다.",
  SAFE: "조명과 팬을 정지하는 모드입니다. 장치 상태를 확인한 뒤 자동 또는 수동을 선택하세요."
};

interface ModeSwitchProps {
  mode: ControlMode;
  available: boolean;
}

export function ModeSwitch({ mode, available }: ModeSwitchProps) {
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
          className={item === mode ? "active" : ""}
          disabled={pending !== null || !available || item === "SAFE" || item === mode}
          key={item}
          type="button"
          aria-pressed={item === mode}
          onClick={() => void changeMode(item)}
        >
          {pending === item ? "변경 중" : labels[item]}
        </button>
      ))}
    </div>
    <p id="mode-description" className="panel-subtitle" style={{ marginTop: 12 }}>
      {available ? descriptions[mode] : "연결 후 작동 방식을 확인하고 변경할 수 있어요."}
    </p>
    {message && available ? <p className="panel-subtitle" role="status" style={{ marginTop: 8 }}>{message}</p> : null}
    {error ? <p className="inline-error" role="alert">{error}</p> : null}
    </div>
  );
}
