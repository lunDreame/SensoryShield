import { getStatus, setFan, setLight, setMode } from "../../shared/api/client";
import type { StatusResponse } from "../../shared/types/domain";

// Acquisition settings for the MVP; these do not define a discomfort threshold.
export const ENVIRONMENT_SAMPLE_COUNT = 20;
const SAMPLE_INTERVAL_MS = 500;
export interface EnvironmentMeasurement {
  luxMedian: number;
  luxMad: number;
  soundMedian: number;
  soundMad: number;
  samples: number;
}
export function summarizeEnvironment(samples: StatusResponse[]): EnvironmentMeasurement {
  if (samples.length < ENVIRONMENT_SAMPLE_COUNT || samples.some(({ sensor, system }) =>
    !system.ready || !sensor.illuminanceValid || !sensor.micValid ||
    !Number.isFinite(sensor.lux) || sensor.lux < 0 ||
    !Number.isFinite(sensor.soundEnergy) || sensor.soundEnergy < 0 || sensor.soundEnergy > 1)) {
    throw new Error("밝기나 소리 센서의 측정값을 확인할 수 없어요. 센서 연결을 확인한 뒤 다시 측정해 주세요.");
  }
  const median = (values: number[]) => {
    const sorted = [...values].sort((a, b) => a - b);
    const mid = Math.floor(sorted.length / 2);
    return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
  };
  const lux = samples.map(({ sensor }) => sensor.lux);
  const sound = samples.map(({ sensor }) => sensor.soundEnergy);
  const luxMedian = median(lux);
  const soundMedian = median(sound);
  return { luxMedian, soundMedian, luxMad: median(lux.map(x => Math.abs(x - luxMedian))),
    soundMad: median(sound.map(x => Math.abs(x - soundMedian))), samples: samples.length };
}

export async function measureEnvironment(onProgress: (count: number) => void): Promise<EnvironmentMeasurement> {
  const original = await getStatus();
  if (!original.system.ready || original.system.mode === "SAFE") {
    throw new Error("지금은 기기를 측정할 수 없어요. 기기 연결과 작동 상태를 확인한 뒤 다시 시도해 주세요.");
  }
  const started = Date.now();
  let changed = false;
  let measurement: EnvironmentMeasurement | undefined;
  let failure: unknown;
  const check = async (command: Promise<{ ok: boolean }>) => {
    if (!(await command).ok) throw new Error("기기가 요청을 처리하지 못했어요. 연결을 확인하고 다시 시도해 주세요.");
  };
  try {
    // Pin both current outputs so automatic control cannot change the reference environment.
    changed = true;
    const output = original.outputs;
    await check(setLight({ power: output.lightOn, brightness: output.brightnessPercent, cct: output.cctMireds,
      rgbMode: output.rgbMode, red: output.red, green: output.green, blue: output.blue }));
    await check(setFan({ power: output.fanOn, speed: output.fanPercent }));
    const samples: StatusResponse[] = [];
    for (let i = 0; i < ENVIRONMENT_SAMPLE_COUNT; i++) {
      await new Promise(resolve => setTimeout(resolve, SAMPLE_INTERVAL_MS));
      const sample = await getStatus();
      // Fail promptly rather than treating disconnected/invalid readings as quiet.
      summarizeEnvironment(Array(ENVIRONMENT_SAMPLE_COUNT).fill(sample));
      if (sample.system.mode !== "MANUAL") throw new Error("측정 중 작동 방식이 바뀌었어요. 다른 조작을 멈추고 다시 측정해 주세요.");
      samples.push(sample);
      onProgress(samples.length);
    }
    measurement = summarizeEnvironment(samples);
  } catch (error) {
    failure = error instanceof Error && /[가-힣]/.test(error.message) ? error :
      new Error("측정 중 기기와 통신하지 못했어요. 연결을 확인한 뒤 다시 측정해 주세요.");
  } finally {
    if (changed) {
      try {
        const remaining = original.system.overrideRemainingSeconds - (Date.now() - started) / 1000;
        const mode = original.system.mode === "OVERRIDE" && remaining <= 0 ? "AUTO" : original.system.mode;
        await check(setMode(mode, mode === "OVERRIDE" ? Math.max(1, Math.ceil(remaining / 60)) : 15));
      } catch {
        throw new Error("이전 작동 방식으로 돌아가지 못했어요. 기기 연결을 확인해 주세요. 연결 후 다시 측정하면 현재 수동 설정이 유지되므로, 완료 후 홈에서 원하는 작동 방식을 선택해 주세요.");
      }
    }
  }
  if (failure) throw failure;
  return measurement!;
}
