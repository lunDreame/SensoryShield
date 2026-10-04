import type { AppConfig, ControlMode, StatusResponse, SystemHealth } from "../types/domain";

export const fallbackStatus: StatusResponse = {
  sensor: {
    lux: 0,
    occupied: false,
    soundEnergy: 0,
    sensoryScore: 0,
    illuminanceValid: false,
    micValid: false,
    pirValid: false
  },
  outputs: {
    lightOn: false,
    brightnessPercent: 0,
    cctMireds: 370,
    fanOn: false,
    fanPercent: 0
  },
  system: {
    ready: false,
    mode: "SAFE",
    overrideRemainingSeconds: 0,
    uptimeSeconds: 0,
    firmware: "local-preview",
    matter: {
      commissioned: false,
      fabricCount: 0,
      threadAttached: false
    },
    storage: {
      appConfig: false,
      deviceTable: false
    }
  }
};

type RawStatusResponse = Omit<Partial<StatusResponse>, "system"> & {
  system?: Partial<Omit<SystemHealth, "mode">> & { mode?: ControlMode | number | string };
};

const modes: ControlMode[] = ["AUTO", "MANUAL", "OVERRIDE", "SAFE"];

function normalizeMode(mode: unknown): ControlMode {
  if (typeof mode === "number") {
    return modes[mode] ?? "SAFE";
  }

  if (typeof mode === "string" && modes.includes(mode as ControlMode)) {
    return mode as ControlMode;
  }

  return "SAFE";
}

function normalizeStatus(raw: RawStatusResponse): StatusResponse {
  return {
    sensor: { ...fallbackStatus.sensor, ...(raw.sensor ?? {}) },
    outputs: { ...fallbackStatus.outputs, ...(raw.outputs ?? {}) },
    system: {
      ...fallbackStatus.system,
      ...(raw.system ?? {}),
      mode: normalizeMode(raw.system?.mode),
      matter: { ...fallbackStatus.system.matter, ...(raw.system?.matter ?? {}) },
      storage: { ...fallbackStatus.system.storage, ...(raw.system?.storage ?? {}) }
    }
  };
}

async function request<T>(path: string, init?: RequestInit, timeoutMs = 2500): Promise<T> {
  const { headers: initHeaders, ...requestInit } = init ?? {};
  const response = await fetch(path, {
    ...requestInit,
    signal: AbortSignal.timeout(timeoutMs),
    headers: { "Content-Type": "application/json", ...(initHeaders ?? {}) }
  });

  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText}`);
  }

  return (await response.json()) as T;
}

export async function getStatus(): Promise<StatusResponse> {
  return normalizeStatus(await request<RawStatusResponse>("/api/status", undefined, 1200));
}

export function setMode(mode: ControlMode, durationMinutes = 15): Promise<{ ok: boolean }> {
  return request("/api/mode", {
    method: "POST",
    body: JSON.stringify({ mode, durationMinutes })
  });
}

export function setLight(payload: {
  power: boolean;
  brightness: number;
  cct: number;
}): Promise<{ ok: boolean }> {
  return request("/api/light", {
    method: "POST",
    body: JSON.stringify(payload)
  });
}

export function setFan(payload: { power: boolean; speed: number }): Promise<{ ok: boolean }> {
  return request("/api/fan", {
    method: "POST",
    body: JSON.stringify(payload)
  });
}

export function saveProfile(config: AppConfig): Promise<{ ok: boolean }> {
  return request("/api/profile", {
    method: "POST",
    body: JSON.stringify(config)
  });
}

export function getProfile(): Promise<AppConfig> {
  return request("/api/profile");
}

export function factoryReset(): Promise<{ ok: boolean }> {
  return request("/api/factory-reset", {
    method: "POST",
    body: JSON.stringify({ confirm: true })
  });
}
