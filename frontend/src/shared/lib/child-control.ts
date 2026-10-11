import type { AppConfig, StatusResponse } from "../types/domain";

export type ComfortAction = "BRIGHT" | "DARK" | "LOUD" | "QUIET";
type Acknowledgement = { ok: boolean };
interface ChildControlApi {
  getStatus: () => Promise<StatusResponse>;
  getProfile: () => Promise<AppConfig>;
  setLight: (payload: { power: boolean; brightness: number; cct: number }) => Promise<Acknowledgement>;
  setFan: (payload: { power: boolean; speed: number }) => Promise<Acknowledgement>;
  setMode: (mode: "OVERRIDE") => Promise<Acknowledgement>;
}

// Product adjustment steps, not sensory/clinical thresholds.
const LIGHT_STEP = 10;
const FAN_STEP = 20;
const FAN_MIN_PERCENT = 18;
const failureMessage = "지금은 기기에 알려주지 못했어요. 연결을 확인하고 다시 눌러주세요.";

export async function applyChildComfort(action: ComfortAction, api: ChildControlApi): Promise<string> {
  let changed = false;
  let stage: "read" | "light" | "fan" | "mode" = "read";
  try {
    // Read immediately before the request: repeated presses must start from the actual latest output.
    const [status, config] = await Promise.all([api.getStatus(), api.getProfile()]);
    if (!status.system.ready || status.system.mode === "SAFE") throw new Error("unavailable");
    const values = [config.minBrightness, config.maxBrightness, config.fanMaxPercent,
      status.outputs.brightnessPercent, status.outputs.fanPercent];
    if (values.some(value => !Number.isInteger(value) || value < 0 || value > 100) ||
        config.minBrightness > config.maxBrightness ||
        !Number.isInteger(config.minCCTMireds) || !Number.isInteger(config.maxCCTMireds) ||
        config.minCCTMireds < 1 || config.maxCCTMireds > 1000 || config.minCCTMireds > config.maxCCTMireds ||
        !Number.isInteger(config.preferredCCTMireds) ||
        config.preferredCCTMireds < config.minCCTMireds || config.preferredCCTMireds > config.maxCCTMireds) {
      throw new Error("invalid state");
    }
    const currentLight = status.outputs.lightOn ? status.outputs.brightnessPercent : 0;
    const currentFan = status.outputs.fanOn ? status.outputs.fanPercent : 0;
    let lightChanged = false;
    let fanChanged = false;
    if (action !== "QUIET") {
      const brightness = action === "DARK"
        ? currentLight >= config.maxBrightness ? currentLight : Math.min(config.maxBrightness, Math.max(config.minBrightness, currentLight + LIGHT_STEP))
        // Never turn an off light on, or raise an already low output, in response to too bright/loud.
        : Math.min(currentLight, Math.max(config.minBrightness, Math.min(config.maxBrightness, currentLight - LIGHT_STEP)));
      const shouldAdjust = action === "DARK" ? currentLight < config.maxBrightness : currentLight > brightness;
      if (shouldAdjust) {
        stage = "light";
        if (!(await api.setLight({ power: brightness > 0, brightness, cct: config.preferredCCTMireds })).ok) throw new Error("light rejected");
        changed = lightChanged = true;
      }
    }
    if (action === "LOUD" || action === "QUIET") {
      let speed = action === "LOUD"
        ? Math.max(0, Math.min(config.fanMaxPercent, currentFan - FAN_STEP))
        : currentFan >= config.fanMaxPercent ? currentFan : Math.min(config.fanMaxPercent, Math.max(FAN_MIN_PERCENT, currentFan + FAN_STEP));
      // Below the motor's minimum duty it does not become quieter: stop it instead.
      if (action === "LOUD" && speed < FAN_MIN_PERCENT) speed = 0;
      if (speed !== currentFan) {
        stage = "fan";
        if (!(await api.setFan({ power: speed > 0, speed })).ok) throw new Error("fan rejected");
        changed = fanChanged = true;
      }
    }
    if (!changed) {
      if (action === "BRIGHT") return currentLight === 0 ? "조명은 이미 꺼져 있어요. 다른 빛이 불편하면 보호자에게 알려주세요." : "설정한 가장 낮은 밝기예요. 더 불편하면 보호자에게 알려주세요.";
      if (action === "DARK") return "설정한 가장 높은 밝기예요. 더 어두우면 보호자에게 알려주세요.";
      if (action === "LOUD") return "조명과 팬을 더 낮출 수 없어요. 다른 소리가 불편하면 보호자에게 알려주세요.";
      return config.fanMaxPercent === 0 ? "보호자가 팬을 꺼두도록 설정했어요." : "설정한 가장 센 바람이에요. 바꾸고 싶으면 보호자에게 알려주세요.";
    }
    stage = "mode";
    // The existing API default supplies the return time; children do not choose a duration.
    if (!(await api.setMode("OVERRIDE")).ok) throw new Error("mode rejected");
    if (action === "BRIGHT") return "조명 밝기를 낮췄어요.";
    if (action === "DARK") return "조명을 조금 더 밝게 했어요.";
    if (action === "QUIET") return "팬을 켜거나 바람 세기를 조금 높였어요.";
    return lightChanged && fanChanged ? "조명 밝기와 팬 세기를 낮췄어요." : lightChanged ? "팬은 이미 꺼져 있어요. 조명 밝기를 낮췄어요." : "팬 세기를 낮췄어요.";
  } catch {
    if (changed && stage === "mode") throw new Error("조절은 됐지만 자동으로 돌아가는 설정을 완료하지 못했어요. 보호자에게 알려주세요.");
    if (changed) {
      // A second actuator failure must not leave the already changed first output in permanent manual mode.
      try {
        if (!(await api.setMode("OVERRIDE")).ok) throw new Error("return rejected");
      } catch {
        throw new Error("일부만 바뀌었고 자동으로 돌아가는 설정도 완료하지 못했어요. 보호자에게 알려주세요.");
      }
      throw new Error("일부만 바뀌었어요. 연결을 확인하고 다시 눌러주세요.");
    }
    throw new Error(failureMessage);
  }
}
