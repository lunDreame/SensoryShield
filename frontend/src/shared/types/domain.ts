export type ControlMode = "AUTO" | "MANUAL" | "OVERRIDE" | "SAFE";

export interface SensorStatus {
  lux: number;
  occupied: boolean;
  soundEnergy: number;
  sensoryScore: number;
  illuminanceValid: boolean;
  micValid: boolean;
  pirValid: boolean;
}

export interface OutputStatus {
  lightOn: boolean;
  brightnessPercent: number;
  cctMireds: number;
  fanOn: boolean;
  fanPercent: number;
}

export interface SystemHealth {
  ready: boolean;
  mode: ControlMode;
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
  fanMaxPercent: number;
  occupancyTimeoutMs: number;
  profileConfigured: boolean;
}
