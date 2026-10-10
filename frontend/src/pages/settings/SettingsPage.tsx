import { useEffect, useState } from "react";
import { getProfile, saveProfile } from "../../shared/api/client";
import { describeKelvin, describeSensitivity } from "../../shared/lib/environment-labels";
import { Panel } from "../../shared/ui/Panel";
import type { AppConfig } from "../../shared/types/domain";

const initialConfig: AppConfig = {
  lightWeight: 0.55,
  soundWeight: 0.45,
  minBrightness: 1,
  maxBrightness: 100,
  minCCTMireds: 250,
  maxCCTMireds: 454,
  fanMaxPercent: 100,
  occupancyTimeoutMs: 30000,
  profileConfigured: true
};

type EditableConfigKey = Exclude<keyof AppConfig, "profileConfigured">;

const labels: Record<EditableConfigKey, { title: string; unit: string }> = {
  lightWeight: { title: "빛 변화 민감도", unit: "%" },
  soundWeight: { title: "소음 변화 민감도", unit: "%" },
  minBrightness: { title: "가장 낮은 자동 밝기", unit: "%" },
  maxBrightness: { title: "가장 높은 자동 밝기", unit: "%" },
  minCCTMireds: { title: "가장 선명한 빛", unit: "K" },
  maxCCTMireds: { title: "가장 따뜻한 빛", unit: "K" },
  fanMaxPercent: { title: "최대 바람 세기", unit: "%" },
  occupancyTimeoutMs: { title: "움직임 후 유지 시간", unit: "초" }
};

interface SettingsPageProps {
  onRestartOnboarding: (config: AppConfig) => void;
}

export function SettingsPage({ onRestartOnboarding }: SettingsPageProps) {
  const [config, setConfig] = useState(initialConfig);
  const [drafts, setDrafts] = useState<Partial<Record<EditableConfigKey, string>>>({});
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

  function displayValue(key: EditableConfigKey) {
    if (key === "occupancyTimeoutMs") return Math.round(config[key] / 1000);
    if (key === "minCCTMireds" || key === "maxCCTMireds") return Math.round(1_000_000 / config[key]);
    if (key === "lightWeight" || key === "soundWeight") return Math.round(config[key] * 100);
    return config[key];
  }

  function update(key: EditableConfigKey, value: number) {
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

  function inputValue(key: EditableConfigKey) {
    return drafts[key] ?? String(displayValue(key));
  }

  function changeInput(key: EditableConfigKey, raw: string) {
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
    if (config.occupancyTimeoutMs < 1000 || config.occupancyTimeoutMs > 600000) {
      setError("움직임 후 유지 시간은 1초에서 10분 사이로 설정해 주세요.");
      return;
    }
    if (config.minCCTMireds > config.maxCCTMireds) {
      setError("최소 색온도는 최대 색온도보다 클 수 없어요.");
      return;
    }
    if (config.minCCTMireds < 250 || config.maxCCTMireds > 454) {
      setError("빛의 따뜻함은 2,203K에서 4,000K 사이로 설정해 주세요.");
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

  async function restartOnboarding() {
    const next = { ...config, profileConfigured: false };
    setPending(true);
    setError(null);
    try {
      await saveProfile(next);
      onRestartOnboarding(next);
    } catch {
      setError("기기와 연결할 수 없어 개인 설정을 시작하지 못했어요.");
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
      <Panel title="개인 맞춤 기준" subtitle="처음 선택한 빛·소리 민감도와 선호 조명을 다시 설정할 수 있어요."
        action={<button className="button button-weak" type="button" disabled={pending} onClick={() => void restartOnboarding()}>개인 설정 다시 하기</button>}>
        <p className="profile-setting-note">다시 답하면 새로운 선택이 자동 조절의 기준으로 저장돼요.</p>
      </Panel>
      <Panel title="자동 반응 민감도" subtitle="평소와 다른 빛과 소리를 감지했을 때 조명이 반응하는 정도">
        <div className="settings-grid">
          {(["lightWeight", "soundWeight"] as EditableConfigKey[]).map((key) => (
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
              <small className="setting-meaning">
                <b>{describeSensitivity(Number(displayValue(key)))}</b>
                {key === "lightWeight" ? " · 높을수록 주변 밝기 변화에 조명이 더 빠르게 반응해요." : " · 높을수록 소음 변화가 조명과 팬 제어에 더 크게 반영돼요."}
              </small>
            </label>
          ))}
        </div>
      </Panel>
      <Panel title="자동 조명 범위" subtitle="자동 조절로 사용할 가장 어두운 값과 가장 밝은 값">
        <div className="settings-grid">
          {(["minBrightness", "maxBrightness", "minCCTMireds", "maxCCTMireds"] as EditableConfigKey[]).map((key) => (
            <label className="input-line" key={key}>
              <span>{labels[key].title}</span>
              <div className="field-with-unit">
                <input type="number" min={key.includes("Brightness") ? 0 : key.includes("CCT") ? 2203 : 1} max={key.includes("Brightness") ? 100 : key.includes("CCT") ? 4000 : 1000} step={key.includes("CCT") ? 100 : 1} value={inputValue(key)} onChange={(event) => changeInput(key, event.target.value)} />
                <em>{labels[key].unit}</em>
              </div>
              {key.includes("CCT") ? <small className="setting-meaning"><b>{describeKelvin(Number(displayValue(key)))}</b> · 숫자가 낮을수록 노란빛, 높을수록 흰빛에 가까워요.</small> : <small className="setting-meaning">자동 조절 중에도 이 범위를 벗어나지 않아요.</small>}
            </label>
          ))}
        </div>
      </Panel>
      <Panel title="바람 세기" subtitle="사용할 수 있는 가장 센 바람을 정해요.">
        <div className="settings-grid">
          {(["fanMaxPercent"] as EditableConfigKey[]).map((key) => (
            <label className="input-line" key={key}>
              <span>{labels[key].title}</span>
              <div className="field-with-unit">
                <input type="number" min={0} max={100} value={inputValue(key)} onChange={(event) => changeInput(key, event.target.value)} />
                <em>{labels[key].unit}</em>
              </div>
              <small style={{ color: "var(--body)", lineHeight: 1.6 }}>0%는 팬을 끄고, 1~100% 범위에서 원하는 바람 세기를 사용할 수 있어요.</small>
            </label>
          ))}
        </div>
      </Panel>
      <Panel title="사람이 없을 때" subtitle="움직임이 멈춘 뒤 자동 조명과 팬을 유지하는 시간">
        <div className="settings-grid">
          <label className="input-line">
            <span>{labels.occupancyTimeoutMs.title}</span>
            <div className="field-with-unit">
              <input type="number" min={1} max={600} value={inputValue("occupancyTimeoutMs")} onChange={(event) => changeInput("occupancyTimeoutMs", event.target.value)} />
              <em>{labels.occupancyTimeoutMs.unit}</em>
            </div>
            <small className="setting-meaning">움직임이 사라진 뒤에도 자동 조절 중인 조명과 팬을 이 시간만큼 더 유지해요.</small>
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
