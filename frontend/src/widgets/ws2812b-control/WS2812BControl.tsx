import { useEffect, useState, type CSSProperties } from "react";
import { setLight, setMode } from "../../shared/api/client";
import { TemporaryApplyActions } from "../../features/temporary-control/TemporaryApplyActions";
import { describeKelvin } from "../../shared/lib/environment-labels";
import { formatDurationMinutes } from "../../shared/lib/duration";
import { Panel } from "../../shared/ui/Panel";
import type { ControlMode, OutputStatus } from "../../shared/types/domain";

interface WS2812BControlProps {
  outputs: OutputStatus;
  available: boolean;
  mode: ControlMode;
}

type LightColorMode = "WHITE" | "RGB";

function toHexChannel(value: number) {
  return Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, "0");
}

function toHexColor(red: number, green: number, blue: number) {
  return `#${toHexChannel(red)}${toHexChannel(green)}${toHexChannel(blue)}`;
}

function fromHexColor(value: string) {
  const normalized = /^#[0-9a-fA-F]{6}$/.test(value) ? value.slice(1) : "ffffff";
  return {
    red: Number.parseInt(normalized.slice(0, 2), 16),
    green: Number.parseInt(normalized.slice(2, 4), 16),
    blue: Number.parseInt(normalized.slice(4, 6), 16)
  };
}

export function WS2812BControl({ outputs, available, mode }: WS2812BControlProps) {
  const [brightness, setBrightness] = useState(outputs.brightnessPercent);
  const [cct, setCct] = useState(outputs.cctMireds);
  const [colorMode, setColorMode] = useState<LightColorMode>(outputs.rgbMode ? "RGB" : "WHITE");
  const [red, setRed] = useState(outputs.red);
  const [green, setGreen] = useState(outputs.green);
  const [blue, setBlue] = useState(outputs.blue);
  const [pending, setPending] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [message, setMessage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!dirty) {
      setBrightness(outputs.brightnessPercent);
      setCct(outputs.cctMireds);
      setColorMode(outputs.rgbMode ? "RGB" : "WHITE");
      setRed(outputs.red);
      setGreen(outputs.green);
      setBlue(outputs.blue);
    }
  }, [outputs.brightnessPercent, outputs.cctMireds, outputs.rgbMode, outputs.red, outputs.green, outputs.blue, dirty]);

  async function commit(applyMode: "MANUAL" | "OVERRIDE", power = outputs.lightOn, durationMinutes = 15) {
    setPending(true);
    setError(null);
    setMessage(null);
    try {
      const lightResult = await setLight({ power, brightness, cct, rgbMode: colorMode === "RGB", red, green, blue });
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

  function markDirty() {
    setDirty(true);
    setMessage(null);
  }

  function updateColor(value: string) {
    const color = fromHexColor(value);
    setRed(color.red);
    setGreen(color.green);
    setBlue(color.blue);
    markDirty();
  }

  const kelvin = Math.round(1_000_000 / cct);
  const hexColor = toHexColor(red, green, blue);

  return (
    <Panel title="WS2812B 조명" subtitle="네오픽셀 링의 밝기, 백색 톤, RGB 색상을 조절합니다." className="control-panel"
      icon={<img className="panel-icon" src="/illustrations/light-control.png" alt="" />}
      action={<span className={`badge mode-badge mode-${mode.toLowerCase()}`}>{{ AUTO: "자동", MANUAL: "수동", OVERRIDE: "잠시 사용", SAFE: "안전" }[mode]} 모드</span>}>
      <div className="control-row">
        <span className={outputs.lightOn ? "badge badge-light-on" : "badge"}>{outputs.lightOn ? "켜짐" : "꺼짐"}</span>
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
          onChange={(event) => { setBrightness(Number(event.target.value)); markDirty(); }}
        />
        <b className="control-value">{brightness}%</b>
      </label>
      <div className="control-caption"><span>은은하게</span><span>밝게</span></div>
      <div className="segmented light-mode-toggle" role="group" aria-label="WS2812B 색상 모드">
        <button className={colorMode === "WHITE" ? "active" : ""} type="button" disabled={!available}
          onClick={() => { setColorMode("WHITE"); markDirty(); }}>색온도</button>
        <button className={colorMode === "RGB" ? "active" : ""} type="button" disabled={!available}
          onClick={() => { setColorMode("RGB"); markDirty(); }}>RGB</button>
      </div>
      {colorMode === "WHITE" ? (
        <>
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
              onChange={(event) => { updateKelvin(Number(event.target.value)); markDirty(); }}
            />
            <b className="control-value">{describeKelvin(kelvin)} · {kelvin.toLocaleString()} K</b>
          </label>
          <div className="control-caption"><span>따뜻하고 차분하게</span><span>하얗고 선명하게</span></div>
        </>
      ) : (
        <div className="rgb-picker">
          <label className="color-field">
            <span>RGB 색상</span>
            <input type="color" value={hexColor} disabled={!available} onChange={(event) => updateColor(event.target.value)} />
            <b>{hexColor.toUpperCase()}</b>
          </label>
          <div className="rgb-values" aria-label="RGB 채널 값">
            <span>R {red}</span>
            <span>G {green}</span>
            <span>B {blue}</span>
          </div>
        </div>
      )}
      <p className="control-apply-help">값을 선택한 뒤 적용 방식을 골라주세요.</p>
      <TemporaryApplyActions disabled={!available || !dirty} pending={pending}
        onApply={(applyMode, durationMinutes) => void commit(applyMode, true, durationMinutes)} />
      {message ? <p className="control-feedback" role="status">{message}</p> : null}
      {error ? <p className="inline-error" role="status">{error}</p> : null}
    </Panel>
  );
}
