// In-memory stand-in for the M5StampS3 backend, used when USE_MOCK is on
// (see api.ts). Lets the UI be reviewed/clicked through with no real device
// on the network. State lives in module-level variables and is mutated the
// same way the real firmware would (kind-based command semantics, computed
// BGM elapsed time instead of a ticking timer, etc.) so the shape of what
// components receive matches production.
import { ApiError } from "./apiError";
import { LED_PRESETS } from "./types";
import type {
  ApiClient,
  BgmPatch,
  BgmState,
  BgmTrack,
  LedPatch,
  LedState,
  RepeatMode,
  RoomState,
  SceneApi,
  SceneExecuteResult,
  SceneLed,
  SensorSettings,
  SettingsResponse,
  SprayState,
  StatusResponse,
  ToiletCommand,
  ToiletCommandKind,
  ToiletState,
} from "./api";

// Flip to true to make every toilet command respond as implemented - useful
// when you just want to click through the whole UI rather than review the
// grayed-out/unimplemented treatment that mirrors the real hardware's
// current IR-code coverage (only "流す(大)" has a collected IR code so far).
const MOCK_ALL_IMPLEMENTED = true;

const MOCK_DELAY_MS = 150;

function wait(ms = MOCK_DELAY_MS): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function clamp(value: number, min: number, max: number) {
  return Math.max(min, Math.min(max, value));
}

const TRACKS: (BgmTrack & { durationSec: number })[] = [
  { id: "1", title: "Ocean Waves", fileName: "001.mp3", durationSec: 214 },
  { id: "2", title: "Morning Coffee", fileName: "002.mp3", durationSec: 190 },
  { id: "3", title: "Forest Rain", fileName: "003.mp3", durationSec: 245 },
  { id: "4", title: "Gentle Piano", fileName: "004.mp3", durationSec: 201 },
  { id: "5", title: "Night Drive", fileName: "005.mp3", durationSec: 228 },
];

const TOILET_COMMAND_KINDS: Record<string, ToiletCommandKind> = {
  flush_large: "momentary",
  flush_small: "momentary",
  flush_eco: "momentary",
  spray_off: "select",
  spray_rear: "select",
  spray_soft: "select",
  spray_bidet: "select",
  water_pressure_up: "step",
  water_pressure_down: "step",
  nozzle_position_forward: "step",
  nozzle_position_backward: "step",
  seat_temp_cycle: "cycle",
  water_temp_cycle: "cycle",
  deodorizer_on: "toggle",
  deodorizer_off: "toggle",
  nozzle_clean: "momentary",
  auto_clean_on: "toggle",
  auto_clean_off: "toggle",
};

// mirrors the real backend's current IR-code coverage (WebUIバックエンド仕様.md)
const IMPLEMENTED_IDS = new Set(["flush_large"]);

let toiletState: ToiletState = {
  spray: "off",
  waterPressure: 3,
  nozzlePosition: 3,
  seatTemp: 1,
  waterTemp: 1,
  deodorizer: false,
  autoClean: true,
};

let ledState: LedState = { on: true, brightness: 60, color: "#fff3d6", presetId: "warm" };

let bgmTrackId = "2";
let bgmVolume = 30;
let bgmRepeat: RepeatMode = "off";
let bgmShuffle = false;
let bgmPlaying = true;
let elapsedAtMark = 42;
let playStartedAt: number | null = Date.now();

let settingsCurrent: SensorSettings = { sensitivity: 20, maxGate: 1, stayDurationSec: 20 };
let settingsSaved: SensorSettings = { ...settingsCurrent };

let scenes: SceneApi[] = [
  { id: "relax", name: "リラックス", primary: true, led: { brightness: 40, color: "#ffcf8a" }, bgmTrackId: "1" },
  { id: "focus", name: "集中", primary: true, led: { brightness: 90, color: "#ffffff" }, bgmTrackId: "2" },
  { id: "party", name: "パーティ", primary: true, led: { brightness: 100, color: "#ff8ad1" }, bgmTrackId: "5" },
  { id: "sleep", name: "おやすみ", primary: false, led: { brightness: 10, color: "#cfeeff" }, bgmTrackId: "3" },
  { id: "guest", name: "来客", primary: false, led: { brightness: 80, color: "#ffffff" }, bgmTrackId: "4" },
];
let nextSceneNumber = 1;

function currentTrack() {
  return TRACKS.find((t) => t.id === bgmTrackId) ?? TRACKS[0];
}

function currentElapsedSec() {
  if (!bgmPlaying || playStartedAt === null) return elapsedAtMark;
  const live = elapsedAtMark + (Date.now() - playStartedAt) / 1000;
  return Math.min(live, currentTrack().durationSec);
}

function snapshotBgm(): BgmState {
  return {
    playing: bgmPlaying,
    trackId: bgmTrackId,
    elapsedSec: Math.floor(currentElapsedSec()),
    durationSec: currentTrack().durationSec,
    volume: bgmVolume,
    repeat: bgmRepeat,
    shuffle: bgmShuffle,
  };
}

function setTrack(id: string) {
  bgmTrackId = id;
  elapsedAtMark = 0;
  playStartedAt = bgmPlaying ? Date.now() : null;
}

function currentRoomState(): RoomState {
  // rotates every 8s purely so the room-state pill can be exercised without a
  // real presence sensor - not meant to simulate realistic dwell timing.
  const cycle = Math.floor(Date.now() / 8000) % 3;
  return (["VACANT", "ENTERING", "STAYING"] as const)[cycle];
}

function applyToiletCommand(id: string): Partial<ToiletState> {
  if (id.startsWith("flush_") || id === "nozzle_clean") return {};
  if (id.startsWith("spray_")) {
    const value = id.slice("spray_".length) as SprayState;
    toiletState = { ...toiletState, spray: value };
    return { spray: value };
  }
  if (id === "water_pressure_up" || id === "water_pressure_down") {
    toiletState = {
      ...toiletState,
      waterPressure: clamp(toiletState.waterPressure + (id.endsWith("up") ? 1 : -1), 1, 5),
    };
    return { waterPressure: toiletState.waterPressure };
  }
  if (id === "nozzle_position_forward" || id === "nozzle_position_backward") {
    toiletState = {
      ...toiletState,
      nozzlePosition: clamp(toiletState.nozzlePosition + (id.endsWith("forward") ? 1 : -1), 1, 5),
    };
    return { nozzlePosition: toiletState.nozzlePosition };
  }
  if (id === "seat_temp_cycle" || id === "water_temp_cycle") {
    const key = id === "seat_temp_cycle" ? "seatTemp" : "waterTemp";
    const next = toiletState[key] >= 3 ? 1 : toiletState[key] + 1;
    toiletState = { ...toiletState, [key]: next };
    return { [key]: next };
  }
  if (id === "deodorizer_on" || id === "deodorizer_off") {
    toiletState = { ...toiletState, deodorizer: id.endsWith("on") };
    return { deodorizer: toiletState.deodorizer };
  }
  if (id === "auto_clean_on" || id === "auto_clean_off") {
    toiletState = { ...toiletState, autoClean: id.endsWith("on") };
    return { autoClean: toiletState.autoClean };
  }
  return {};
}

function applyLedPatch(partial: LedPatch): LedState {
  let { brightness, color, presetId } = ledState;
  if (partial.presetId !== undefined) {
    presetId = partial.presetId;
    const preset = LED_PRESETS.find((p) => p.id === partial.presetId);
    if (preset) color = preset.color;
  }
  if (partial.color !== undefined) {
    color = partial.color;
    presetId = null;
  }
  if (partial.brightness !== undefined) {
    brightness = partial.brightness;
  }
  ledState = { on: brightness > 0, brightness, color, presetId };
  return { ...ledState };
}

function applyBgmPatch(partial: BgmPatch): BgmState {
  if (partial.trackId !== undefined && partial.trackId !== bgmTrackId) {
    setTrack(partial.trackId);
    bgmPlaying = true;
    playStartedAt = Date.now();
  }
  if (partial.seekSec !== undefined) {
    elapsedAtMark = clamp(partial.seekSec, 0, currentTrack().durationSec);
    playStartedAt = bgmPlaying ? Date.now() : null;
  }
  if (partial.volume !== undefined) bgmVolume = clamp(partial.volume, 0, 100);
  if (partial.repeat !== undefined) bgmRepeat = partial.repeat;
  if (partial.shuffle !== undefined) bgmShuffle = partial.shuffle;
  if (partial.action === "play" && !bgmPlaying) {
    bgmPlaying = true;
    playStartedAt = Date.now();
  } else if (partial.action === "pause" && bgmPlaying) {
    elapsedAtMark = currentElapsedSec();
    bgmPlaying = false;
    playStartedAt = null;
  }
  return snapshotBgm();
}

function skipTrack(direction: 1 | -1): BgmState {
  const idx = TRACKS.findIndex((t) => t.id === bgmTrackId);
  setTrack(TRACKS[(idx + direction + TRACKS.length) % TRACKS.length].id);
  bgmPlaying = true;
  playStartedAt = Date.now();
  return snapshotBgm();
}

export const mockApi: ApiClient = {
  async getStatus(): Promise<StatusResponse> {
    await wait();
    return {
      roomState: currentRoomState(),
      presence: { raw: true, smoothed: true },
      toilet: { ...toiletState },
      led: { ...ledState },
      bgm: snapshotBgm(),
      settings: { ...settingsCurrent },
      wifi: { connected: true, ip: "192.168.11.42" },
      mqtt: { enabled: false, connected: false },
      wled: { reachable: true },
      uptimeMs: Date.now(),
    };
  },

  async getSettings(): Promise<SettingsResponse> {
    await wait();
    return {
      current: { ...settingsCurrent },
      saved: { ...settingsSaved },
      dirty: JSON.stringify(settingsCurrent) !== JSON.stringify(settingsSaved),
    };
  },
  async putSettings(partial): Promise<SettingsResponse> {
    await wait();
    settingsCurrent = { ...settingsCurrent, ...partial };
    return {
      current: { ...settingsCurrent },
      saved: { ...settingsSaved },
      dirty: JSON.stringify(settingsCurrent) !== JSON.stringify(settingsSaved),
    };
  },
  async saveSettings(): Promise<void> {
    await wait();
    settingsSaved = { ...settingsCurrent };
  },

  async getToiletCommands(): Promise<{ commands: ToiletCommand[] }> {
    await wait();
    const commands = Object.entries(TOILET_COMMAND_KINDS).map(([id, kind]) => ({
      id,
      kind,
      implemented: MOCK_ALL_IMPLEMENTED || IMPLEMENTED_IDS.has(id),
    }));
    return { commands };
  },
  async getToiletState(): Promise<ToiletState> {
    await wait();
    return { ...toiletState };
  },
  async executeToiletCommand(id) {
    await wait();
    if (!(id in TOILET_COMMAND_KINDS)) throw new ApiError(404, "not_found", "unknown command");
    if (!(MOCK_ALL_IMPLEMENTED || IMPLEMENTED_IDS.has(id))) {
      throw new ApiError(501, "not_implemented", "IR RAWデータ未収録");
    }
    return { id, state: applyToiletCommand(id) };
  },

  async getLed(): Promise<LedState> {
    await wait();
    return { ...ledState };
  },
  async putLed(partial): Promise<LedState> {
    await wait();
    return applyLedPatch(partial);
  },

  async getBgm(): Promise<BgmState> {
    await wait();
    return snapshotBgm();
  },
  async putBgm(partial): Promise<BgmState> {
    await wait();
    return applyBgmPatch(partial);
  },
  async bgmNext(): Promise<BgmState> {
    await wait();
    return skipTrack(1);
  },
  async bgmPrevious(): Promise<BgmState> {
    await wait();
    return skipTrack(-1);
  },
  async getBgmTracks(): Promise<{ tracks: BgmTrack[] }> {
    await wait();
    return { tracks: TRACKS.map(({ id, title, fileName }) => ({ id, title, fileName })) };
  },

  async getScenes(): Promise<{ scenes: SceneApi[] }> {
    await wait();
    return { scenes: [...scenes] };
  },
  async executeScene(id): Promise<SceneExecuteResult> {
    await wait();
    const scene = scenes.find((s) => s.id === id);
    if (!scene) throw new ApiError(404, "not_found", "unknown scene");
    applyLedPatch({ brightness: scene.led.brightness, color: scene.led.color });
    setTrack(scene.bgmTrackId);
    bgmPlaying = true;
    playStartedAt = Date.now();
    return { led: { ...ledState }, bgm: snapshotBgm() };
  },
  async createScene(payload: { name: string; led: SceneLed; bgmTrackId: string }): Promise<SceneApi> {
    await wait();
    const scene: SceneApi = {
      id: `custom-${nextSceneNumber++}`,
      name: payload.name,
      primary: false,
      led: payload.led,
      bgmTrackId: payload.bgmTrackId,
    };
    scenes = [...scenes, scene];
    return scene;
  },
};
