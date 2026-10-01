import { useState } from "react";
import { setMode } from "../../shared/api/client";
import type { ControlMode } from "../../shared/types/domain";

const modes: ControlMode[] = ["AUTO", "MANUAL", "OVERRIDE", "SAFE"];
const labels: Record<ControlMode, string> = {
  AUTO: "자동",
  MANUAL: "수동",
  OVERRIDE: "잠시 사용",
  SAFE: "안전"
};

interface ModeSwitchProps {
  mode: ControlMode;
  available: boolean;
}

export function ModeSwitch({ mode, available }: ModeSwitchProps) {
  const [pending, setPending] = useState<ControlMode | null>(null);
  const [error, setError] = useState<string | null>(null);

  async function changeMode(next: ControlMode) {
    setPending(next);
    setError(null);
    try {
      await setMode(next);
    } catch {
      setError("기기와 연결할 수 없어 제어 모드를 변경하지 못했어요.");
    } finally {
      setPending(null);
    }
  }

  return (
    <div>
    <div className="segmented" aria-label="제어 모드">
      {modes.map((item) => (
        <button
          className={item === mode ? "active" : ""}
          disabled={pending !== null || !available || item === "SAFE"}
          key={item}
          type="button"
          aria-pressed={item === mode}
          onClick={() => void changeMode(item)}
        >
          {pending === item ? "변경 중" : labels[item]}
        </button>
      ))}
    </div>
    {error ? <p className="inline-error" role="status">{error}</p> : null}
    </div>
  );
}
