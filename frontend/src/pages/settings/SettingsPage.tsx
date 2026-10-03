import { useEffect, useState } from "react";
import { getProfile, saveProfile } from "../../shared/api/client";
import { Panel } from "../../shared/ui/Panel";
import type { AppConfig } from "../../shared/types/domain";

const initialConfig: AppConfig = {
  lightWeight: 0.55,
  soundWeight: 0.45,
  minBrightness: 5,
  maxBrightness: 85,
  minCCTMireds: 250,
  maxCCTMireds: 454,
  fanMaxPercent: 80,
  occupancyTimeoutMs: 300000
};

const labels: Record<keyof AppConfig, { title: string; unit: string }> = {
  lightWeight: { title: "빛 반영 정도", unit: "%" },
  soundWeight: { title: "소리 반영 정도", unit: "%" },
  minBrightness: { title: "최소 밝기", unit: "%" },
  maxBrightness: { title: "최대 밝기", unit: "%" },
  minCCTMireds: { title: "차가운 빛 색온도", unit: "K" },
  maxCCTMireds: { title: "따뜻한 빛 색온도", unit: "K" },
  fanMaxPercent: { title: "바람 세기 한도", unit: "%" },
  occupancyTimeoutMs: { title: "사람이 없을 때 유지", unit: "초" }
};

export function SettingsPage() {
  const [config, setConfig] = useState(initialConfig);
  const [drafts, setDrafts] = useState<Partial<Record<keyof AppConfig, string>>>({});
  const [state, setState] = useState("변경 사항 없음");
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    void getProfile().then((stored) => {
      if (active) {
        setConfig(stored);
        setDrafts({});
        setState("저장된 설정을 불러왔어요");
      }
    }).catch(() => {
      if (active) {
        setError("기기와 연결되지 않아 기본 설정을 표시하고 있어요.");
      }
    });
    return () => { active = false; };
  }, []);

  function displayValue(key: keyof AppConfig) {
    if (key === "occupancyTimeoutMs") return config[key] / 1000;
    if (key === "minCCTMireds" || key === "maxCCTMireds") return Math.round(1_000_000 / config[key]);
    if (key === "lightWeight" || key === "soundWeight") return Math.round(config[key] * 100);
    return config[key];
  }

  function update(key: keyof AppConfig, value: number) {
    const storedValue = key === "occupancyTimeoutMs"
      ? value * 1000
      : key === "minCCTMireds" || key === "maxCCTMireds"
        ? Math.round(1_000_000 / value)
        : key === "lightWeight" || key === "soundWeight"
          ? value / 100
          : value;
    const next = { ...config, [key]: storedValue };
    setConfig(next);
    setError(null);
    setState("변경됨");
  }

  function inputValue(key: keyof AppConfig) {
    return drafts[key] ?? String(displayValue(key));
  }

  function changeInput(key: keyof AppConfig, raw: string) {
    setDrafts((current) => ({ ...current, [key]: raw }));
    if (raw === "") return;
    const value = Number(raw);
    if (Number.isFinite(value)) update(key, value);
  }

  async function submit() {
    if (Object.values(drafts).some((value) => value === "")) {
      setError("비어 있는 항목을 입력해 주세요.");
      return;
    }
    if (config.lightWeight + config.soundWeight <= 0) {
      setError("빛이나 소리 반영 정도를 0보다 크게 설정해 주세요.");
      return;
    }
    if (config.minBrightness > config.maxBrightness) {
      setError("최소 밝기는 최대 밝기보다 클 수 없어요.");
      return;
    }
    if (config.lightWeight < 0 || config.lightWeight > 1 || config.soundWeight < 0 || config.soundWeight > 1) {
      setError("빛과 소리 반영 정도는 0에서 100% 사이로 설정해 주세요.");
      return;
    }
    if (config.minBrightness < 0 || config.maxBrightness > 100 || config.fanMaxPercent < 0 || config.fanMaxPercent > 100) {
      setError("밝기와 바람 세기는 0에서 100% 사이로 설정해 주세요.");
      return;
    }
    if (config.occupancyTimeoutMs < 1000 || config.occupancyTimeoutMs > 86400000) {
      setError("유지 시간은 1초에서 24시간 사이로 설정해 주세요.");
      return;
    }
    if (config.minCCTMireds > config.maxCCTMireds) {
      setError("최소 색온도는 최대 색온도보다 클 수 없어요.");
      return;
    }
    if (config.minCCTMireds < 250 || config.maxCCTMireds > 454) {
      setError("색온도는 2,203K에서 4,000K 사이로 설정해 주세요.");
      return;
    }

    setPending(true);
    setError(null);
    try {
      await saveProfile(config);
      setState("저장됨");
    } catch {
      setState("저장 실패");
      setError("기기와 연결할 수 없어 설정을 저장하지 못했어요.");
    } finally {
      setPending(false);
    }
  }

  return (
    <main className="page-grid">
      <section className="overview">
        <div className="overview-copy">
          <h1>설정</h1>
          <p>공간에 맞게 조명과 바람의 범위를 정해보세요.</p>
        </div>
      </section>
      <Panel title="빛과 소리 반응" subtitle="자동 조절에서 빛과 소리를 반영하는 정도">
        <div className="settings-grid">
          {(["lightWeight", "soundWeight"] as Array<keyof AppConfig>).map((key) => (
            <label className="input-line" key={key}>
              <span>{labels[key].title}</span>
              <div className="field-with-unit">
                <input
                  type="number"
                  min={0}
                  max={100}
                  step={5}
                  value={inputValue(key)}
                  onChange={(event) => changeInput(key, event.target.value)}
                />
                <em>{labels[key].unit}</em>
              </div>
              {key === "soundWeight" ? <small style={{ color: "var(--body)", lineHeight: 1.6 }}>높을수록 소음 변화에 민감하게 반응합니다. 0%는 소음에 따른 팬 감속을 끕니다.</small> : null}
            </label>
          ))}
        </div>
      </Panel>
      <Panel title="조명 범위" subtitle="자동 제어에서 사용할 밝기와 색온도">
        <div className="settings-grid">
          {(["minBrightness", "maxBrightness", "minCCTMireds", "maxCCTMireds"] as Array<keyof AppConfig>).map((key) => (
            <label className="input-line" key={key}>
              <span>{labels[key].title}</span>
              <div className="field-with-unit">
                <input type="number" min={key.includes("Brightness") ? 0 : key.includes("CCT") ? 2203 : 1} max={key.includes("Brightness") ? 100 : key.includes("CCT") ? 4000 : 1000} step={key.includes("CCT") ? 100 : 1} value={inputValue(key)} onChange={(event) => changeInput(key, event.target.value)} />
                <em>{labels[key].unit}</em>
              </div>
            </label>
          ))}
        </div>
      </Panel>
      <Panel title="바람 세기" subtitle="팬 속도의 명령 상한을 설정하세요.">
        <div className="settings-grid">
          {(["fanMaxPercent"] as Array<keyof AppConfig>).map((key) => (
            <label className="input-line" key={key}>
              <span>{labels[key].title}</span>
              <div className="field-with-unit">
                <input type="number" min={0} max={100} value={inputValue(key)} onChange={(event) => changeInput(key, event.target.value)} />
                <em>{labels[key].unit}</em>
              </div>
              <small style={{ color: "var(--body)", lineHeight: 1.6 }}>0%는 자동 팬을 끕니다. 현재 최소 구동 기준은 18%이며, 그보다 낮은 한도에서는 자동 팬이 정지합니다.</small>
            </label>
          ))}
        </div>
      </Panel>
      <Panel title="사람이 없을 때" subtitle="움직임이 감지되지 않은 뒤에도 자동 조절을 유지하는 시간">
        <div className="settings-grid">
          <label className="input-line">
            <span>{labels.occupancyTimeoutMs.title}</span>
            <div className="field-with-unit">
              <input type="number" min={1} max={86400} value={inputValue("occupancyTimeoutMs")} onChange={(event) => changeInput("occupancyTimeoutMs", event.target.value)} />
              <em>{labels.occupancyTimeoutMs.unit}</em>
            </div>
          </label>
        </div>
      </Panel>
      <div className="settings-actions page-spacer">
        <span className="save-state" role="status">{state}</span>
        <button className="button button-primary" type="button" disabled={pending} onClick={() => void submit()}>{pending ? "저장 중" : "변경 사항 저장"}</button>
      </div>
      {error ? <div className="notice notice-error" role="alert"><i className="notice-dot" />{error}</div> : null}
    </main>
  );
}
