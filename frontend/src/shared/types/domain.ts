export type ControlMode = "AUTO" | "MANUAL" | "OVERRIDE" | "SAFE";

export interface SensorStatus {
  lux: number;
  occupied: boolean;
  soundEnergy: number;
  sensoryScore: number;
  baselineReady?: boolean;
  illuminanceValid: boolean;
  micValid: boolean;
  pirValid: boolean;
}

export interface OutputStatus {
  lightOn: boolean;
  brightnessPercent: number;
  cctMireds: number;
  rgbMode: boolean;
  red: number;
  green: number;
  blue: number;
  fanOn: boolean;
  fanPercent: number;
}

export interface SystemHealth {
  ready: boolean;
  mode: ControlMode;
  overrideRemainingSeconds: number;
  uptimeSeconds: number;
  firmware: string;
  matter: {
    commissioned: boolean;
    fabricCount: number;
    threadAttached: boolean;
  };
  storage: {
    appConfig: boolean;
    deviceTable: boolean;
  };
}

export interface StatusResponse {
  sensor: SensorStatus;
  outputs: OutputStatus;
  system: SystemHealth;
}

export interface AppConfig {
  lightWeight: number;
  soundWeight: number;
  minBrightness: number;
  maxBrightness: number;
  minCCTMireds: number;
  maxCCTMireds: number;
  preferredCCTMireds: number;
  preferredBrightness: number;
  preferredFanPercent: number;
  normalCCTMireds: number;
  reliefFanPercent: number;
  reliefHoldMs: number;
  recoverConfirmMs: number;
  fanMaxPercent: number;
  occupancyTimeoutMs: number;
  occupancyAutomationEnabled: boolean;
  profileConfigured: boolean;
}

export interface EnvironmentMeasurement {
  state: "idle" | "collecting" | "ready" | "error";
  samples: number;
  requiredSamples: number;
  luxMedian: number;
  luxMad: number;
  soundMedian: number;
  soundMad: number;
  hasSavedInitialEnvironment: boolean;
  savedLuxMedian: number;
  savedSoundMedian: number;
}
