import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import {
  api,
  ApiError,
  type BgmState,
  type BgmTrack,
  type LedState,
  type RoomState,
  type SceneApi,
  type SceneLed,
  type StatusResponse,
  type ToiletCommand,
  type ToiletState,
} from "../api";
import { useToast } from "../components/Toast";

const POLL_INTERVAL_MS = 1000;
const OFFLINE_AFTER_FAILURES = 3;
const PATCH_DEBOUNCE_MS = 120;

type LedPatch = Partial<Pick<LedState, "brightness" | "color" | "presetId">>;
type BgmPatch = Parameters<typeof api.putBgm>[0];

interface BackendContextValue {
  online: boolean;
  roomState: RoomState | null;
  toilet: ToiletState | null;
  led: LedState | null;
  bgm: BgmState | null;
  wifi: StatusResponse["wifi"] | null;
  mqtt: StatusResponse["mqtt"] | null;
  wled: StatusResponse["wled"] | null;
  toiletCommands: Record<string, ToiletCommand>;
  scenes: SceneApi[];
  tracks: BgmTrack[];
  executeToiletCommand: (id: string) => Promise<boolean>;
  isImplemented: (id: string) => boolean;
  updateLed: (partial: LedPatch) => void;
  updateBgm: (partial: BgmPatch) => void;
  bgmNext: () => void;
  bgmPrevious: () => void;
  executeScene: (id: string) => void;
  createScene: (payload: { name: string; led: SceneLed; bgmTrackId: string }) => Promise<boolean>;
}

const BackendContext = createContext<BackendContextValue | null>(null);

function networkErrorToast(showToast: (msg: string) => void, e: unknown, fallback: string) {
  if (e instanceof ApiError && e.status !== 0) {
    showToast(fallback);
  } else {
    showToast("マイコンと通信できません");
  }
}

export function BackendProvider({ children }: { children: ReactNode }) {
  const { showToast } = useToast();
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [online, setOnline] = useState(true);
  const [toiletOverride, setToiletOverride] = useState<Partial<ToiletState>>({});
  const [ledOverride, setLedOverride] = useState<Partial<LedState>>({});
  const [bgmOverride, setBgmOverride] = useState<Partial<BgmState>>({});
  const [toiletCommands, setToiletCommands] = useState<Record<string, ToiletCommand>>({});
  const [scenes, setScenes] = useState<SceneApi[]>([]);
  const [tracks, setTracks] = useState<BgmTrack[]>([]);
  const failuresRef = useRef(0);

  const ledPendingRef = useRef<LedPatch>({});
  const ledTimerRef = useRef<number | undefined>(undefined);
  const bgmPendingRef = useRef<BgmPatch>({});
  const bgmTimerRef = useRef<number | undefined>(undefined);

  useEffect(() => {
    let cancelled = false;
    api
      .getToiletCommands()
      .then((res) => {
        if (cancelled) return;
        const map: Record<string, ToiletCommand> = {};
        for (const c of res.commands) map[c.id] = c;
        setToiletCommands(map);
      })
      .catch(() => {});
    api
      .getScenes()
      .then((res) => {
        if (!cancelled) setScenes(res.scenes);
      })
      .catch(() => {});
    api
      .getBgmTracks()
      .then((res) => {
        if (!cancelled) setTracks(res.tracks);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    let cancelled = false;
    let timer: number;

    const poll = async () => {
      try {
        const next = await api.getStatus();
        if (cancelled) return;
        failuresRef.current = 0;
        setOnline(true);
        setStatus(next);
        setToiletOverride({});
        setLedOverride({});
        setBgmOverride({});
      } catch {
        if (cancelled) return;
        failuresRef.current += 1;
        if (failuresRef.current >= OFFLINE_AFTER_FAILURES) setOnline(false);
      } finally {
        if (!cancelled) timer = window.setTimeout(poll, POLL_INTERVAL_MS);
      }
    };

    poll();
    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, []);

  const isImplemented = useCallback(
    (id: string) => toiletCommands[id]?.implemented ?? true,
    [toiletCommands],
  );

  const executeToiletCommand = useCallback(
    async (id: string) => {
      try {
        const res = await api.executeToiletCommand(id);
        setToiletOverride((prev) => ({ ...prev, ...res.state }));
        return true;
      } catch (e) {
        if (e instanceof ApiError) {
          if (e.status === 501) showToast("この操作は未対応です");
          else if (e.status === 409) showToast("少し待ってから操作してください");
          else if (e.status === 0) showToast("マイコンと通信できません");
          else showToast("操作に失敗しました");
        } else {
          showToast("マイコンと通信できません");
        }
        return false;
      }
    },
    [showToast],
  );

  const updateLed = useCallback(
    (partial: LedPatch) => {
      setLedOverride((prev) => ({ ...prev, ...partial }));
      ledPendingRef.current = { ...ledPendingRef.current, ...partial };
      window.clearTimeout(ledTimerRef.current);
      ledTimerRef.current = window.setTimeout(() => {
        const toSend = ledPendingRef.current;
        ledPendingRef.current = {};
        api
          .putLed(toSend)
          .then((res) => setLedOverride(res))
          .catch((e) => networkErrorToast(showToast, e, "LEDに反映できませんでした"));
      }, PATCH_DEBOUNCE_MS);
    },
    [showToast],
  );

  const updateBgm = useCallback(
    (partial: BgmPatch) => {
      setBgmOverride((prev) => ({ ...prev, ...partial }));
      bgmPendingRef.current = { ...bgmPendingRef.current, ...partial };
      window.clearTimeout(bgmTimerRef.current);
      bgmTimerRef.current = window.setTimeout(() => {
        const toSend = bgmPendingRef.current;
        bgmPendingRef.current = {};
        api
          .putBgm(toSend)
          .then((res) => setBgmOverride(res))
          .catch((e) => networkErrorToast(showToast, e, "BGMの操作に失敗しました"));
      }, PATCH_DEBOUNCE_MS);
    },
    [showToast],
  );

  const bgmNext = useCallback(() => {
    api
      .bgmNext()
      .then((res) => setBgmOverride(res))
      .catch(() => showToast("マイコンと通信できません"));
  }, [showToast]);

  const bgmPrevious = useCallback(() => {
    api
      .bgmPrevious()
      .then((res) => setBgmOverride(res))
      .catch(() => showToast("マイコンと通信できません"));
  }, [showToast]);

  const executeScene = useCallback(
    (id: string) => {
      api
        .executeScene(id)
        .then((res) => {
          if (res.led) setLedOverride(res.led);
          if (res.bgm) setBgmOverride(res.bgm);
          if (res.ledError) showToast("シーンのLED反映に失敗しました");
          if (res.bgmError) showToast("シーンのBGM反映に失敗しました");
        })
        .catch(() => showToast("マイコンと通信できません"));
    },
    [showToast],
  );

  const createScene = useCallback(
    async (payload: { name: string; led: SceneLed; bgmTrackId: string }) => {
      try {
        const scene = await api.createScene(payload);
        setScenes((prev) => [...prev, scene]);
        return true;
      } catch {
        showToast("シーンの保存に失敗しました");
        return false;
      }
    },
    [showToast],
  );

  const toilet = status
    ? ({ ...status.toilet, ...toiletOverride } as ToiletState)
    : Object.keys(toiletOverride).length
      ? (toiletOverride as ToiletState)
      : null;
  const led = status
    ? ({ ...status.led, ...ledOverride } as LedState)
    : Object.keys(ledOverride).length
      ? (ledOverride as LedState)
      : null;
  const bgm = status
    ? ({ ...status.bgm, ...bgmOverride } as BgmState)
    : Object.keys(bgmOverride).length
      ? (bgmOverride as BgmState)
      : null;

  const value: BackendContextValue = {
    online,
    roomState: status?.roomState ?? null,
    toilet,
    led,
    bgm,
    wifi: status?.wifi ?? null,
    mqtt: status?.mqtt ?? null,
    wled: status?.wled ?? null,
    toiletCommands,
    scenes,
    tracks,
    executeToiletCommand,
    isImplemented,
    updateLed,
    updateBgm,
    bgmNext,
    bgmPrevious,
    executeScene,
    createScene,
  };

  return <BackendContext.Provider value={value}>{children}</BackendContext.Provider>;
}

export function useBackend() {
  const ctx = useContext(BackendContext);
  if (!ctx) throw new Error("useBackend must be used within BackendProvider");
  return ctx;
}
