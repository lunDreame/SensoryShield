import { useEffect, useRef, useState, type CSSProperties } from "react";
import { getProfile, setFan, setMode } from "../../shared/api/client";
import { TemporaryApplyActions } from "../../features/temporary-control/TemporaryApplyActions";
import { formatDurationMinutes } from "../../shared/lib/duration";
import { Panel } from "../../shared/ui/Panel";
import "./fan-control.css";
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

  const modeLabel = { AUTO: "자동", MANUAL: "수동", OVERRIDE: "잠시 사용", SAFE: "안전" }[mode];
  const rangeProgress = limit !== null && limit > 18 ? (selectedSpeed - 18) / (limit - 18) * 100 : 0;

  async function commit(power: boolean, applyMode: "MANUAL" | "OVERRIDE" = "MANUAL", durationMinutes = 15) {
    if (requestInFlight.current || !available || (power && !canStart)) return;
    requestInFlight.current = true;
    setPending(true);
    setError(null);
    setMessage(null);
    try {
      const result = await setFan({ power, speed: power ? selectedSpeed : 0 });
      if (!result.ok) throw new Error("rejected");
      if (applyMode === "OVERRIDE") {
        const modeResult = await setMode("OVERRIDE", durationMinutes);
        if (!modeResult.ok) throw new Error("rejected");
      }
      setDirty(false);
      setMessage(power ? applyMode === "OVERRIDE" ? `이 바람을 ${formatDurationMinutes(durationMinutes)} 동안 사용한 뒤 개인 맞춤 자동으로 돌아가요.` : "이 바람을 계속 유지해요. 자동으로 돌아가려면 작동 방식에서 선택해 주세요." : "정지 명령을 전송했어요. 수신된 현재 상태를 확인해 주세요.");
    } catch {
      setError("명령 전송 실패 · 선택값은 유지됩니다. 연결 확인 후 다시 적용해 주세요.");
    } finally {
      requestInFlight.current = false;
      setPending(false);
    }
  }

  return (
    <Panel title="팬" subtitle="바람 세기를 확인하고 조절하세요." className="control-panel fan-panel"
      icon={<img className="panel-icon" src="/illustrations/fan-control.png" alt="" />}
      action={<span className={`badge fan-mode mode-badge mode-${mode.toLowerCase()}`}>{modeLabel} 모드</span>}>
      <div className="fan-current">
        <div>
          <span className="fan-label">{available ? "현재 속도" : "마지막 수신 속도"}</span>
          <div className="fan-reading"><strong>{outputs.fanOn ? outputs.fanPercent : 0}</strong><span>%</span></div>
        </div>
        <div className="fan-power">
          <span className={outputs.fanOn ? "badge badge-fan-on" : "badge"}>{outputs.fanOn ? "동작 중" : "정지"}</span>
          <button className="button button-weak" type="button"
            disabled={pending || !available || (!outputs.fanOn && !canStart)}
            onClick={() => void commit(!outputs.fanOn)}>
            {pending ? "전송 중" : outputs.fanOn ? "정지" : "시작"}
          </button>
        </div>
      </div>
      <p className="fan-mode-note">
        {mode === "AUTO" ? "자동 제어 중 · 지속 소음이 커지면 팬 속도를 낮춥니다." : mode === "OVERRIDE" ? "임시 수동 제어 중 · 직접 조절하면 수동 모드로 바뀝니다." : mode === "MANUAL" ? "수동 제어 중 · 자동 복귀는 아래 작동 방식에서 선택하세요." : "안전 모드 · 자동 또는 수동 모드를 선택하세요."}
      </p>
      <div className="fan-draft">
        <div className="fan-draft-heading">
          <label htmlFor="fan-speed" className="fan-label">적용할 속도</label>
          <div className="fan-selection"><b>{canStart ? `${selectedSpeed}%` : "--"}</b>
            {dirty ? <span className="fan-unsaved">적용 전</span> : null}</div>
        </div>
        <div className="slider fan-slider">
          <input id="fan-speed" aria-describedby="fan-speed-help" max={Math.max(18, limit ?? 100)} min={18}
            disabled={!canStart || pending}
            style={{ "--range-progress": `${rangeProgress}%` } as CSSProperties}
            type="range" value={Math.max(18, selectedSpeed)}
            onChange={(event) => { setSpeed(Number(event.target.value)); setDirty(true); setMessage(null); }} />
        </div>
        <div className="fan-range-labels"><span>{canStart ? "18%" : "--"}</span>
          <span>{limit === null ? "상한 확인 중" : `설정 상한 ${limit}%`}</span></div>
        <p id="fan-speed-help" className="fan-help">속도를 선택한 뒤 적용 방식을 골라주세요.</p>
        <TemporaryApplyActions disabled={!canStart || !dirty} pending={pending}
          onApply={(applyMode, durationMinutes) => void commit(true, applyMode, durationMinutes)} />
      </div>
      {limit !== null && limit < 18 ? <p className="fan-feedback">팬 사용이 제한돼 있어요. 설정에서 한도를 18% 이상으로 높여주세요.</p> : null}
      {message ? <p className="fan-feedback fan-success" role="status">{message}</p> : null}
      {error ? <p className="inline-error fan-feedback" role="alert">{error}</p> : null}
      {available && limit === null && error ? <button className="button button-weak" type="button" onClick={() => { setError(null); setProfileAttempt((value) => value + 1); }}>팬 상한 다시 확인</button> : null}
    </Panel>
  );
}
