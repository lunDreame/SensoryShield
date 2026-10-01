import { useEffect, useState, type CSSProperties } from "react";
import { setLight } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import type { OutputStatus } from "../../shared/types/domain";

interface CCTLightControlProps {
  outputs: OutputStatus;
  available: boolean;
}

export function CCTLightControl({ outputs, available }: CCTLightControlProps) {
  const [brightness, setBrightness] = useState(outputs.brightnessPercent);
  const [cct, setCct] = useState(outputs.cctMireds);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    setBrightness(outputs.brightnessPercent);
    setCct(outputs.cctMireds);
  }, [outputs.brightnessPercent, outputs.cctMireds]);

  async function commit(power = outputs.lightOn) {
    setPending(true);
    setError(null);
    try {
      await setLight({ power, brightness, cct });
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
    <Panel title="조명" subtitle="밝기와 빛 색깔을 조절합니다." className="control-panel">
      <div className="control-row">
        <span className={outputs.lightOn ? "badge badge-blue" : "badge"}>{outputs.lightOn ? "켜짐" : "꺼짐"}</span>
        <button className="button button-weak" type="button" disabled={pending || !available} onClick={() => void commit(!outputs.lightOn)}>
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
          onChange={(event) => setBrightness(Number(event.target.value))}
          onPointerUp={() => commit(true)}
          onKeyUp={() => void commit(true)}
        />
        <b className="control-value">{brightness}%</b>
      </label>
      <div className="control-caption"><span>은은하게</span><span>밝게</span></div>
      <label className="slider">
        빛 색깔
        <input
          max={4000}
          min={2200}
          step={50}
          disabled={!available}
          type="range"
          value={kelvin}
          style={{ "--range-progress": `${((kelvin - 2200) / 1800) * 100}%` } as CSSProperties}
          onChange={(event) => updateKelvin(Number(event.target.value))}
          onPointerUp={() => commit(true)}
          onKeyUp={() => void commit(true)}
        />
        <b className="control-value">{kelvin.toLocaleString()} K</b>
      </label>
      <div className="control-caption"><span>따뜻하게</span><span>밝고 선명하게</span></div>
      {error ? <p className="inline-error" role="status">{error}</p> : null}
    </Panel>
  );
}
