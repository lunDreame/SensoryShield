import { useEffect, useRef, useState, type CSSProperties } from "react";
import { getProfile, setFan } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import type { ControlMode, OutputStatus } from "../../shared/types/domain";

interface FanControlProps {
  outputs: OutputStatus;
  available: boolean;
  mode: ControlMode;
}

export function FanControl({ outputs, available, mode }: FanControlProps) {
  const [speed, setSpeed] = useState(outputs.fanPercent || 30);
  const [limit, setLimit] = useState<number | null>(null);
  const [dirty, setDirty] = useState(false);
  const [pending, setPending] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [profileAttempt, setProfileAttempt] = useState(0);
  const requestInFlight = useRef(false);

  useEffect(() => {
    if (!dirty && outputs.fanPercent > 0) setSpeed(outputs.fanPercent);
  }, [outputs.fanPercent, dirty]);

  useEffect(() => {
    let active = true;
    setLimit(null);
    if (available) {
      void getProfile().then((profile) => {
        if (active) { setLimit(profile.fanMaxPercent); setError(null); }
      }).catch(() => {
        if (active) setError("팬 상한을 불러오지 못했어요. 연결 복구 후 다시 확인합니다.");
      });
    }
    return () => { active = false; };
  }, [available, profileAttempt]);

  const canStart = available && limit !== null && limit >= 18;
  const selectedSpeed = Math.min(limit ?? 100, Math.max(18, speed));

  async function commit(power: boolean) {
    if (requestInFlight.current || !available || (power && !canStart)) return;
    requestInFlight.current = true;
    setPending(true);
    setError(null);
    setMessage(null);
    try {
      const result = await setFan({ power, speed: power ? selectedSpeed : 0 });
      if (!result.ok) throw new Error("rejected");
      setDirty(false);
      setMessage("명령을 전송했어요. 수동 모드로 전환되며 실제 상태는 다음 수신값으로 확인합니다.");
    } catch {
      setError("팬 명령을 보내지 못했어요. 선택값은 유지됩니다. 연결을 확인하고 다시 적용해 주세요.");
    } finally {
      requestInFlight.current = false;
      setPending(false);
    }
  }

  return (
    <Panel title="팬" subtitle="현재 상태를 확인하고 바람 세기를 조절합니다." className="control-panel">
      <div className="control-row">
        <span className={outputs.fanOn ? "badge badge-blue" : "badge"}>{outputs.fanOn ? `동작 · ${outputs.fanPercent}%` : "정지"}</span>
        <button className="button button-weak" type="button"
          disabled={pending || !available || (!outputs.fanOn && !canStart)}
          onClick={() => void commit(!outputs.fanOn)}>
          {pending ? "전송 중" : outputs.fanOn ? "정지" : "시작"}
        </button>
      </div>
      <p className="panel-subtitle">
        {mode === "AUTO" ? "자동 제어 중 · 지속 소음이 커지면 팬 속도를 낮춥니다." : mode === "OVERRIDE" ? "임시 수동 제어 중 · 팬을 직접 조절하면 수동 모드로 전환됩니다." : mode === "MANUAL" ? "수동 제어 중 · 자동으로 돌아가려면 작동 방식을 변경하세요." : "안전 모드"}
      </p>
      <label className="slider">
        적용할 속도
        <input max={Math.max(18, limit ?? 100)} min={18} disabled={!canStart || pending}
          style={{ "--range-progress": `${selectedSpeed}%` } as CSSProperties}
          type="range" value={Math.max(18, selectedSpeed)}
          onChange={(event) => { setSpeed(Number(event.target.value)); setDirty(true); setMessage(null); }} />
        <b className="control-value">{canStart ? `${selectedSpeed}%` : "--"}</b>
      </label>
      <div className="control-row">
        <span className="panel-subtitle">{limit === null ? "팬 상한 확인 대기" : `설정 상한 ${limit}%`}{dirty ? " · 적용 전" : ""}</span>
        <button className="button button-primary" type="button" disabled={!canStart || pending}
          onClick={() => void commit(true)}>{pending ? "전송 중" : "속도 적용"}</button>
      </div>
      {limit !== null && limit < 18 ? <p className="panel-subtitle">설정 상한이 최소 구동 기준 18%보다 낮아요. 팬을 사용하려면 설정에서 한도를 높여주세요.</p> : null}
      {message ? <p className="panel-subtitle" role="status">{message}</p> : null}
      {error ? <p className="inline-error" role="alert">{error}</p> : null}
      {available && limit === null && error ? <button className="button button-weak" type="button" onClick={() => { setError(null); setProfileAttempt((value) => value + 1); }}>팬 상한 다시 확인</button> : null}
    </Panel>
  );
}
