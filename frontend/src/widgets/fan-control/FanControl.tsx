import { useEffect, useState, type CSSProperties } from "react";
import { setFan } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import type { OutputStatus } from "../../shared/types/domain";

interface FanControlProps {
  outputs: OutputStatus;
  available: boolean;
}

export function FanControl({ outputs, available }: FanControlProps) {
  const [speed, setSpeed] = useState(outputs.fanPercent);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    setSpeed(outputs.fanPercent);
  }, [outputs.fanPercent]);

  async function commit(power = outputs.fanOn) {
    setPending(true);
    setError(null);
    try {
      await setFan({ power, speed });
    } catch {
      setError("팬 명령을 보내지 못했어요. 기기 연결을 확인해 주세요.");
    } finally {
      setPending(false);
    }
  }

  return (
    <Panel title="팬" subtitle="바람 세기를 조절합니다." className="control-panel">
      <div className="control-row">
        <span className={outputs.fanOn ? "badge badge-blue" : "badge"}>{outputs.fanOn ? "동작" : "정지"}</span>
        <button className="button button-weak" type="button" disabled={pending || !available} onClick={() => void commit(!outputs.fanOn)}>
          {outputs.fanOn ? "정지" : "시작"}
        </button>
      </div>
      <label className="slider">
        속도
        <input
          max={100}
          min={0}
          disabled={!available}
          style={{ "--range-progress": `${speed}%` } as CSSProperties}
          type="range"
          value={speed}
          onChange={(event) => setSpeed(Number(event.target.value))}
          onPointerUp={() => commit(true)}
          onKeyUp={() => void commit(true)}
        />
        <b className="control-value">{speed}%</b>
      </label>
      {error ? <p className="inline-error" role="status">{error}</p> : null}
    </Panel>
  );
}
