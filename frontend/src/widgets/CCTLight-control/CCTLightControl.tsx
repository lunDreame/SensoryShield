import { useEffect, useState, type CSSProperties } from "react";
import { setLight, setMode } from "../../shared/api/client";
import { TemporaryApplyActions } from "../../features/temporary-control/TemporaryApplyActions";
import { describeKelvin } from "../../shared/lib/environment-labels";
import { formatDurationMinutes } from "../../shared/lib/duration";
import { Panel } from "../../shared/ui/Panel";
import type { ControlMode, OutputStatus } from "../../shared/types/domain";

interface CCTLightControlProps {
  outputs: OutputStatus;
  available: boolean;
  mode: ControlMode;
}

export function CCTLightControl({ outputs, available, mode }: CCTLightControlProps) {
  const [brightness, setBrightness] = useState(outputs.brightnessPercent);
  const [cct, setCct] = useState(outputs.cctMireds);
  const [pending, setPending] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!dirty) {
      setBrightness(outputs.brightnessPercent);
      setCct(outputs.cctMireds);
    }
  }, [outputs.brightnessPercent, outputs.cctMireds, dirty]);

  async function commit(applyMode: "MANUAL" | "OVERRIDE", power = outputs.lightOn, durationMinutes = 15) {
    setPending(true);
    setError(null);
    setMessage(null);
    try {
      const lightResult = await setLight({ power, brightness, cct });
      if (!lightResult.ok) throw new Error("rejected");
      if (applyMode === "OVERRIDE") {
        const modeResult = await setMode("OVERRIDE", durationMinutes);
        if (!modeResult.ok) throw new Error("rejected");
      }
      setDirty(false);
      setMessage(applyMode === "OVERRIDE" ? `이 조명을 ${formatDurationMinutes(durationMinutes)} 동안 사용한 뒤 개인 맞춤 자동으로 돌아가요.` : "이 조명을 계속 유지해요. 자동으로 돌아가려면 작동 방식에서 선택해 주세요.");
    } catch {
      setError("조명 명령을 보내지 못했어요. 기기 연결을 확인해 주세요.");
    } finally {
      setPending(false);
    }
  }

  function updateKelvin(value: number) {
    setCct(Math.round(1_000_000 / value));
  }

  const kelvin = Math.round(1_000_000 / cct);

  return (
    <Panel title="조명" subtitle="밝기와 빛 색깔을 조절합니다." className="control-panel"
      action={<span className="badge">{{ AUTO: "자동", MANUAL: "수동", OVERRIDE: "잠시 사용", SAFE: "안전" }[mode]} 모드</span>}>
      <div className="control-row">
        <span className={outputs.lightOn ? "badge badge-blue" : "badge"}>{outputs.lightOn ? "켜짐" : "꺼짐"}</span>
        <button className="button button-weak" type="button" disabled={pending || !available} onClick={() => void commit("MANUAL", !outputs.lightOn)}>
          {outputs.lightOn ? "끄기" : "켜기"}
        </button>
      </div>
      <label className="slider">
        밝기
        <input
          max={100}
          min={0}
          disabled={!available}
          style={{ "--range-progress": `${brightness}%` } as CSSProperties}
          type="range"
          value={brightness}
          onChange={(event) => { setBrightness(Number(event.target.value)); setDirty(true); setMessage(null); }}
        />
        <b className="control-value">{brightness}%</b>
      </label>
      <div className="control-caption"><span>은은하게</span><span>밝게</span></div>
      <label className="slider">
        빛의 따뜻함
        <input
          max={4000}
          min={2200}
          step={50}
          disabled={!available}
          type="range"
          value={kelvin}
          style={{ "--range-progress": `${((kelvin - 2200) / 1800) * 100}%` } as CSSProperties}
          onChange={(event) => { updateKelvin(Number(event.target.value)); setDirty(true); setMessage(null); }}
        />
        <b className="control-value">{describeKelvin(kelvin)} · {kelvin.toLocaleString()} K</b>
      </label>
      <div className="control-caption"><span>따뜻하고 차분하게</span><span>하얗고 선명하게</span></div>
      <p className="control-apply-help">값을 선택한 뒤 적용 방식을 골라주세요.</p>
      <TemporaryApplyActions disabled={!available || !dirty} pending={pending}
        onApply={(applyMode, durationMinutes) => void commit(applyMode, true, durationMinutes)} />
      {message ? <p className="control-feedback" role="status">{message}</p> : null}
      {error ? <p className="inline-error" role="status">{error}</p> : null}
    </Panel>
  );
}
