import { useEffect, useState } from "react";
import { getProfile, saveProfile } from "../../shared/api/client";
import { describeKelvin, describeSensitivity } from "../../shared/lib/environment-labels";
import { demoTimingDefaults } from "../../shared/lib/demo-defaults";
import { Panel } from "../../shared/ui/Panel";
import type { AppConfig } from "../../shared/types/domain";

const initialConfig: AppConfig = {
  lightWeight: 0.55,
  soundWeight: 0.45,
  minBrightness: 30,
  maxBrightness: 100,
  minCCTMireds: 250,
  maxCCTMireds: 454,
  preferredCCTMireds: 454,
  preferredBrightness: 100,
  preferredFanPercent: 100,
  normalCCTMireds: 250, reliefFanPercent: 0,
  ...demoTimingDefaults,
  fanMaxPercent: 100,
  occupancyAutomationEnabled: false,
  profileConfigured: true
};

type EditableConfigKey = Exclude<keyof AppConfig, "profileConfigured" | "occupancyAutomationEnabled">;

const labels: Record<EditableConfigKey, { title: string; unit: string }> = {
  lightWeight: { title: "빛 변화 민감도", unit: "%" },
  soundWeight: { title: "소음 변화 민감도", unit: "%" },
  minBrightness: { title: "완화 시 밝기 하한", unit: "%" },
  maxBrightness: { title: "자동 밝기 상한", unit: "%" },
  minCCTMireds: { title: "허용 색온도 상한", unit: "K" },
  maxCCTMireds: { title: "허용 색온도 하한", unit: "K" },
  preferredBrightness: { title: "평소 조명 밝기", unit: "%" },
  preferredCCTMireds: { title: "자극 완화 시 사용할 빛", unit: "K" },
  preferredFanPercent: { title: "평소 팬 세기", unit: "%" },
  normalCCTMireds: { title: "평소 빛의 색온도", unit: "K" },
  reliefFanPercent: { title: "완화 시 팬 목표 세기", unit: "%" },
  reliefHoldMs: { title: "완화 최소 유지 시간", unit: "초" },
  recoverConfirmMs: { title: "복귀 확인 시간", unit: "초" },
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
    if (key.endsWith("Ms")) return config[key] / 1000;
    if (key.includes("CCT")) return Math.round(1_000_000 / config[key]);
    if (key === "lightWeight" || key === "soundWeight") return Math.round(config[key] * 100);
    return config[key];
  }

  function update(key: EditableConfigKey, value: number) {
    const storedValue = key.endsWith("Ms")
      ? value * 1000
      : key.includes("CCT")
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
    if (!Number.isInteger(config.preferredBrightness) || config.preferredBrightness < config.minBrightness ||
        config.preferredBrightness > config.maxBrightness) {
      setError("평소 밝기를 최소·최대 밝기 범위 안에서 선택해 주세요."); return;
    }
    if (config.lightWeight < 0 || config.lightWeight > 1 || config.soundWeight < 0 || config.soundWeight > 1) {
      setError("빛과 소리 반영 정도는 0에서 100% 사이로 설정해 주세요.");
      return;
    }
    if (config.minBrightness < 0 || config.maxBrightness > 100 || config.fanMaxPercent < 0 || config.fanMaxPercent > 100) {
      setError("밝기와 바람 세기는 0에서 100% 사이로 설정해 주세요.");
      return;
    }
    if (!Number.isInteger(config.preferredFanPercent) || config.preferredFanPercent < 0 || config.preferredFanPercent > config.fanMaxPercent) {
      setError("평소 바람 세기를 0에서 최대 바람 세기 사이로 설정해 주세요."); return;
    }
    if (config.occupancyTimeoutMs < 1000 || config.occupancyTimeoutMs > 3600000) {
      setError("움직임 후 유지 시간은 1초에서 60분 사이로 설정해 주세요.");
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
    if (!Number.isFinite(config.preferredCCTMireds) || config.preferredCCTMireds < config.minCCTMireds ||
        config.preferredCCTMireds > config.maxCCTMireds) {
      setError("자극 완화 시 사용할 색온도를 설정한 색온도 범위 안에서 선택해 주세요.");
      return;
    }

    if (!Number.isInteger(config.normalCCTMireds) || config.normalCCTMireds < config.minCCTMireds || config.normalCCTMireds > config.maxCCTMireds) {
      setError("평소 색온도를 허용 색온도 범위 안에서 선택해 주세요."); return;
    }
    if (!Number.isInteger(config.reliefFanPercent) || config.reliefFanPercent < 0 || config.reliefFanPercent > 100 || (config.reliefFanPercent > 0 && config.reliefFanPercent < 18)) {
      setError("완화 시 팬 세기는 0%(끄기) 또는 18~100%로 설정해 주세요."); return;
    }
    if (!Number.isInteger(config.reliefHoldMs) || config.reliefHoldMs < 0 || config.reliefHoldMs > 3600000) {
      setError("완화 최소 유지 시간은 0초에서 60분 사이로 설정해 주세요."); return;
    }
    if (!Number.isInteger(config.recoverConfirmMs) || config.recoverConfirmMs < 250 || config.recoverConfirmMs > 60000) {
      setError("복귀 확인 시간은 0.25초에서 60초 사이로 설정해 주세요."); return;
    }
    setPending(true);
    setError(null);
    try {
      if (!(await saveProfile(config)).ok) throw new Error("save rejected");
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

  const meanings: Partial<Record<EditableConfigKey, string>> = {
    minBrightness: "빛과 소리 변화가 커졌을 때 낮출 수 있는 최소 밝기예요.",
    preferredCCTMireds: "조명을 낮출 때 사용할 빛이에요. 따뜻한 빛과 선명한 빛 중 아이에게 맞게 정해주세요.",
    reliefFanPercent: "빛이나 소리 변화가 커지면 이 세기로 줄여요. 평소보다 높이지 않으며, 0%는 팬을 꺼요.",
    reliefHoldMs: "조명과 팬을 낮춘 상태로 유지할 최소 시간이에요. 빛이나 소리 변화가 크면 더 오래 유지해요.",
    recoverConfirmMs: "빛과 소리 변화가 작게 유지되는지 확인할 시간이에요. 최소 유지 시간도 지나면 평소 밝기와 바람으로 서서히 돌아가요.",
    preferredBrightness: "자동 모드에서 평소 사용할 밝기이며 완화 후 이 밝기로 돌아와요.",
    normalCCTMireds: "평소 사용할 빛이에요. 완화할 때 사용할 빛과 따로 정할 수 있어요.",
    preferredFanPercent: "자동 모드에서 평소 사용할 바람 세기예요. 0%는 팬을 꺼둬요.",
    maxBrightness: "평소 밝기와 아이의 밝기 요청에도 적용하는 상한이에요.",
    fanMaxPercent: "평소·완화·수동 요청에 적용하는 팬 목표의 상한이에요. 켤 때는 회전을 시작하기 위해 0.35초 동안 100%로 구동한 뒤 목표 세기로 돌아와요.",
    minCCTMireds: "평소와 완화 색온도를 선택할 수 있는 가장 높은 K 값이에요.",
    maxCCTMireds: "평소와 완화 색온도를 선택할 수 있는 가장 낮은 K 값이에요.",
    occupancyTimeoutMs: "움직임이 사라진 뒤에도 자동 조절 중인 조명과 팬을 이 시간만큼 더 유지해요."
  };
  function field(key: EditableConfigKey) {
    const cct = key.includes("CCT");
    const time = key.endsWith("Ms");
    const sensitivity = key === "lightWeight" || key === "soundWeight";
    const minimum = cct ? 2203 : key === "recoverConfirmMs" ? .25 : key === "occupancyTimeoutMs" ? 1 : 0;
    const maximum = cct ? 4000 : key === "recoverConfirmMs" ? 60 : key === "reliefHoldMs" ? 3600 : time ? 3600 : 100;
    return <label className="input-line" key={key}>
      <span>{labels[key].title}</span>
      <div className="field-with-unit">
        <input type="number" min={minimum} max={maximum} step={time && key !== "occupancyTimeoutMs" ? .25 : sensitivity ? 5 : 1}
          disabled={pending || (key === "occupancyTimeoutMs" && !config.occupancyAutomationEnabled)} value={inputValue(key)} onChange={event => changeInput(key, event.target.value)} />
        <em>{labels[key].unit}</em>
      </div>
      <small className="setting-meaning">{sensitivity ? <><b>{describeSensitivity(Number(displayValue(key)))}</b>
        {key === "lightWeight" ? " · 높을수록 밝아질 때 조명과 팬을 더 크게 조절해요." : " · 높을수록 소리가 커질 때 조명과 팬을 더 크게 조절해요."}</> :
        <>{cct ? <><b>{describeKelvin(Number(displayValue(key)))}</b> · </> : null}{meanings[key]}</>}</small>
    </label>;
  }
  return <main className="page-grid">
    <section className="overview"><div className="overview-copy"><h1>설정</h1><p>평소 밝기와 바람, 불편할 때 조절할 범위를 정해보세요.</p></div></section>
    <Panel title="개인 맞춤 기준" subtitle="처음 설문조사의 빛·소리 민감도와 완화 색온도를 다시 선택할 수 있어요."
      action={<button className="button button-weak" type="button" disabled={pending} onClick={() => void restartOnboarding()}>개인 설정 다시 하기</button>}>
      <p className="profile-setting-note">다시 답해도 평소 밝기·바람과 유지 시간은 유지돼요. 저장하지 않은 변경 사항은 먼저 저장해 주세요.</p>
    </Panel>
    <Panel title="자동 반응 민감도" subtitle="평소보다 밝아지거나 소리가 커졌을 때 반응하는 정도예요.">
      <div className="settings-grid">{field("lightWeight")}{field("soundWeight")}</div>
      <p className="profile-setting-note settings-section-note">평소 주변 환경은 자동으로 학습해요. 밝기·소리가 감소했다는 이유만으로 자극 점수가 높아지지는 않아요.</p>
    </Panel>
    <Panel title="자극 완화 범위" subtitle="자극이 커졌을 때 조명과 팬을 얼마나 낮추고, 어떤 빛으로 바꿀지 정해요.">
      <div className="settings-grid">{(["minBrightness", "preferredCCTMireds", "reliefFanPercent", "reliefHoldMs", "recoverConfirmMs"] as EditableConfigKey[]).map(field)}</div>
      <details className="settings-output-limits"><summary>밝기와 바람의 허용 범위</summary>
        <p className="profile-setting-note">평소와 자극을 줄일 때 모두 적용해요.</p>
        <div className="settings-grid">{(["maxBrightness", "fanMaxPercent", "minCCTMireds", "maxCCTMireds"] as EditableConfigKey[]).map(field)}</div>
      </details>
    </Panel>
    <Panel title="자동 모드의 평소 설정" subtitle="자동 모드에서 평소 사용할 밝기와 바람이에요. 자극이 줄어들면 이 설정으로 서서히 돌아가요.">
      <div className="settings-grid">{(["preferredBrightness", "normalCCTMireds", "preferredFanPercent"] as EditableConfigKey[]).map(field)}</div>
      <p className="profile-setting-note settings-section-note">평소 설정을 바꾸면 새 밝기와 바람에 맞춰 주변 환경을 다시 익혀요. 홈에서 직접 조절한 값은 평소 설정에 저장되지 않아요.</p>
    </Panel>
    <Panel title="재실 확인 설정" subtitle="자동 모드에서 움직임에 따라 조명과 팬을 끄고 다시 시작할지 정해요.">
      <label className="occupancy-toggle">
        <span className="occupancy-toggle-copy"><strong>재실 자동화</strong><small>움직임이 없으면 자동 종료하고, 다시 감지되면 자동 제어를 시작해요.</small></span>
        <input type="checkbox" role="switch" aria-label="재실 자동화" checked={config.occupancyAutomationEnabled} disabled={pending}
          onChange={event => { const enabled = event.currentTarget.checked; setConfig(current => ({ ...current, occupancyAutomationEnabled: enabled })); setError(null); setState("변경됨"); }} />
        <span className="occupancy-switch-track" aria-hidden="true" />
        <span className="occupancy-toggle-state" aria-hidden="true">{config.occupancyAutomationEnabled ? "켜짐" : "꺼짐"}</span>
      </label>
      <p className="profile-setting-note settings-section-note">{config.occupancyAutomationEnabled
        ? "아래 유지 시간 동안 움직임이 감지되지 않으면 조명과 팬을 꺼요. 다시 움직임이 감지되면 자동 모드의 설정에 맞춰 켜져요."
        : "움직임이 없어도 자동 제어를 계속해요. 종료하려면 홈에서 조명과 팬을 직접 꺼주세요. 움직임 감지는 계속해요."}</p>
      <p className="profile-setting-note settings-section-note">움직임 센서는 가만히 앉거나 누워 있으면 감지되지 않을 수 있어요. 직접 끈 기기는 움직임이 감지돼도 켜지지 않아요.</p>
      <div className="settings-grid">{field("occupancyTimeoutMs")}</div>
    </Panel>
    <div className="settings-actions page-spacer"><span className="save-state" role="status">{state}</span>
      <button className="button button-primary" type="button" disabled={pending} onClick={() => void submit()}>{pending ? "저장 중" : "변경 사항 저장"}</button></div>
    {error ? <div className="notice notice-error" role="alert"><i className="notice-dot" />{error}</div> : null}
  </main>;
}
