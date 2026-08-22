// HTTP client for the M5StampS3 backend. See WebUIバックエンド仕様.md for the
// authoritative endpoint/response definitions this file implements.

import { mockApi } from "./mockApi";

export type RoomState = "VACANT" | "ENTERING" | "STAYING";

export type SprayState = "off" | "rear" | "soft" | "bidet";

export interface ToiletState {
  spray: SprayState;
  waterPressure: number;
  nozzlePosition: number;
  seatTemp: number;
  waterTemp: number;
  deodorizer: boolean;
  autoClean: boolean;
}

export type ToiletCommandKind = "momentary" | "select" | "toggle" | "step" | "cycle";

export interface ToiletCommand {
  id: string;
  kind: ToiletCommandKind;
  implemented: boolean;
}

// minimum interval between repeated executions of the same command, keyed by kind
export const COOLDOWN_MS: Record<ToiletCommandKind, number> = {
  momentary: 2000,
  select: 300,
  toggle: 300,
  step: 150,
  cycle: 150,
};

export interface LedState {
  on: boolean;
  brightness: number;
  color: string;
  presetId: string | null;
}

export type RepeatMode = "off" | "one" | "all";

export interface BgmState {
  playing: boolean;
  trackId: string;
  elapsedSec: number;
  durationSec: number;
  volume: number;
  repeat: RepeatMode;
  shuffle: boolean;
}

export interface BgmTrack {
  id: string;
  title: string;
  fileName: string;
}

export interface SceneLed {
  brightness: number;
  color: string;
}

export interface SceneApi {
  id: string;
  name: string;
  primary: boolean;
  led: SceneLed;
  bgmTrackId: string;
}

export interface SceneExecuteResult {
  led: LedState | null;
  ledError?: { code: string; message?: string };
  bgm: BgmState | null;
  bgmError?: { code: string; message?: string };
}

export interface SensorSettings {
  sensitivity: number;
  maxGate: number;
  stayDurationSec: number;
}

export interface SettingsResponse {
  current: SensorSettings;
  saved: SensorSettings;
  dirty: boolean;
}

export interface StatusResponse {
  roomState: RoomState;
  presence: { raw: boolean; smoothed: boolean };
  toilet: ToiletState;
  led: LedState;
  bgm: BgmState;
  settings: SensorSettings;
  wifi: { connected: boolean; ip?: string };
  mqtt: { enabled: boolean; connected: boolean };
  wled: { reachable: boolean };
  uptimeMs: number;
}

import { ApiError } from "./apiError";
export { ApiError };

export type BgmPatch = Partial<{
  action: "play" | "pause";
  volume: number;
  trackId: string;
  seekSec: number;
  repeat: RepeatMode;
  shuffle: boolean;
}>;

export type LedPatch = Partial<Pick<LedState, "brightness" | "color" | "presetId">>;

export interface ApiClient {
  getStatus: () => Promise<StatusResponse>;
  getSettings: () => Promise<SettingsResponse>;
  putSettings: (partial: Partial<SensorSettings>) => Promise<SettingsResponse>;
  saveSettings: () => Promise<void>;
  getToiletCommands: () => Promise<{ commands: ToiletCommand[] }>;
  getToiletState: () => Promise<ToiletState>;
  executeToiletCommand: (id: string) => Promise<{ id: string; state: Partial<ToiletState> }>;
  getLed: () => Promise<LedState>;
  putLed: (partial: LedPatch) => Promise<LedState>;
  getBgm: () => Promise<BgmState>;
  putBgm: (partial: BgmPatch) => Promise<BgmState>;
  bgmNext: () => Promise<BgmState>;
  bgmPrevious: () => Promise<BgmState>;
  getBgmTracks: () => Promise<{ tracks: BgmTrack[] }>;
  getScenes: () => Promise<{ scenes: SceneApi[] }>;
  executeScene: (id: string) => Promise<SceneExecuteResult>;
  createScene: (payload: { name: string; led: SceneLed; bgmTrackId: string }) => Promise<SceneApi>;
}

// same-origin by default (the ESP32 is expected to serve the built frontend
// itself); see vite.config.ts for the dev-server proxy used with `npm run dev`.
const BASE = "";

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let res: Response;
  try {
    res = await fetch(`${BASE}${path}`, {
      headers: { "Content-Type": "application/json; charset=utf-8" },
      ...init,
    });
  } catch {
    throw new ApiError(0, "network_error", "マイコンと通信できません");
  }

  if (!res.ok) {
    let code: string | undefined;
    let message = `HTTP ${res.status}`;
    try {
      const body = await res.json();
      code = body?.error?.code;
      message = body?.error?.message ?? message;
    } catch {
      // no JSON error body
    }
    throw new ApiError(res.status, code, message);
  }

  if (res.status === 204) return undefined as T;
  const text = await res.text();
  return (text ? JSON.parse(text) : undefined) as T;
}

const realApi: ApiClient = {
  getStatus: () => request<StatusResponse>("/api/status"),

  getSettings: () => request<SettingsResponse>("/api/settings"),
  putSettings: (partial) =>
    request<SettingsResponse>("/api/settings", { method: "PUT", body: JSON.stringify(partial) }),
  saveSettings: () => request<void>("/api/settings/save", { method: "POST" }),

  getToiletCommands: () => request<{ commands: ToiletCommand[] }>("/api/toilet/commands"),
  getToiletState: () => request<ToiletState>("/api/toilet/state"),
  executeToiletCommand: (id) =>
    request<{ id: string; state: Partial<ToiletState> }>(`/api/toilet/commands/${id}/execute`, {
      method: "POST",
    }),

  getLed: () => request<LedState>("/api/led"),
  putLed: (partial) => request<LedState>("/api/led", { method: "PUT", body: JSON.stringify(partial) }),

  getBgm: () => request<BgmState>("/api/bgm"),
  putBgm: (partial) => request<BgmState>("/api/bgm", { method: "PUT", body: JSON.stringify(partial) }),
  bgmNext: () => request<BgmState>("/api/bgm/next", { method: "POST" }),
  bgmPrevious: () => request<BgmState>("/api/bgm/previous", { method: "POST" }),
  getBgmTracks: () => request<{ tracks: BgmTrack[] }>("/api/bgm/tracks"),

  getScenes: () => request<{ scenes: SceneApi[] }>("/api/scenes"),
  executeScene: (id) => request<SceneExecuteResult>(`/api/scenes/${id}/execute`, { method: "POST" }),
  createScene: (payload) => request<SceneApi>("/api/scenes", { method: "POST", body: JSON.stringify(payload) }),
};

// Mock mode lets the UI run with fabricated, self-consistent data and no
// M5StampS3 on the network - for reviewing layout/interactions only.
// Enable via `VITE_USE_MOCK=true` (see package.json's `dev:mock` script) or,
// without restarting the dev server, by loading the page with `?mock=1`
// (`?mock=0` forces the real client even if the env var is set).
function resolveUseMock(): boolean {
  if (typeof window !== "undefined") {
    const param = new URLSearchParams(window.location.search).get("mock");
    if (param === "1" || param === "true") return true;
    if (param === "0" || param === "false") return false;
  }
  return import.meta.env.VITE_USE_MOCK === "true";
}

export const USE_MOCK = resolveUseMock();

export const api: ApiClient = USE_MOCK ? mockApi : realApi;
