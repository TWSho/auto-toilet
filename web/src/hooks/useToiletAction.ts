import { useEffect, useState } from "react";
import { COOLDOWN_MS } from "../api";
import { useBackend } from "../context/BackendContext";

const DEFAULT_COOLDOWN_MS = 300;

/**
 * One instance per card: tracks per-command-id cooldown windows locally so
 * buttons disable immediately on tap instead of waiting for a 409 response.
 */
export function useToiletAction() {
  const { executeToiletCommand, isImplemented, toiletCommands } = useBackend();
  const [cooldowns, setCooldowns] = useState<Record<string, number>>({});
  const [, forceTick] = useState(0);

  useEffect(() => {
    const upcoming = Object.values(cooldowns).filter((t) => t > Date.now());
    if (upcoming.length === 0) return;
    const soonest = Math.min(...upcoming);
    const timer = window.setTimeout(() => forceTick((v) => v + 1), Math.max(0, soonest - Date.now()));
    return () => window.clearTimeout(timer);
  }, [cooldowns]);

  const isOnCooldown = (id: string) => (cooldowns[id] ?? 0) > Date.now();
  const disabledFor = (id: string) => !isImplemented(id) || isOnCooldown(id);

  const run = async (id: string) => {
    if (disabledFor(id)) return false;
    const kind = toiletCommands[id]?.kind;
    const ms = kind ? COOLDOWN_MS[kind] : DEFAULT_COOLDOWN_MS;
    setCooldowns((prev) => ({ ...prev, [id]: Date.now() + ms }));
    return executeToiletCommand(id);
  };

  return { run, disabledFor };
}
