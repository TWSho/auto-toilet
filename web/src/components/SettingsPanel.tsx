import { useEffect, useRef, useState } from "react";
import GlassButton from "./GlassButton";
import { CloseIcon, SettingsIcon } from "./Icons";
import { api, type SensorSettings } from "../api";
import { useToast } from "./Toast";

const PATCH_DEBOUNCE_MS = 150;

const FIELDS: { key: keyof SensorSettings; label: string; min: number; max: number; step: number; suffix?: string }[] = [
  { key: "sensitivity", label: "感度", min: 0, max: 100, step: 10 },
  { key: "maxGate", label: "検知距離（ゲート）", min: 1, max: 8, step: 1 },
  { key: "stayDurationSec", label: "滞在時間閾値", min: 5, max: 60, step: 5, suffix: "秒" },
];

export default function SettingsPanel() {
  const [open, setOpen] = useState(false);
  const [current, setCurrent] = useState<SensorSettings | null>(null);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const { showToast } = useToast();

  const pendingRef = useRef<Partial<SensorSettings>>({});
  const timerRef = useRef<number | undefined>(undefined);

  useEffect(() => {
    if (!open) return;
    let cancelled = false;
    api
      .getSettings()
      .then((res) => {
        if (cancelled) return;
        setCurrent(res.current);
        setDirty(res.dirty);
      })
      .catch(() => showToast("設定を取得できませんでした"));
    return () => {
      cancelled = true;
    };
  }, [open, showToast]);

  const patch = (key: keyof SensorSettings, value: number) => {
    setCurrent((prev) => (prev ? { ...prev, [key]: value } : prev));
    setDirty(true);
    pendingRef.current = { ...pendingRef.current, [key]: value };
    window.clearTimeout(timerRef.current);
    timerRef.current = window.setTimeout(() => {
      const toSend = pendingRef.current;
      pendingRef.current = {};
      api
        .putSettings(toSend)
        .then((res) => {
          setCurrent(res.current);
          setDirty(res.dirty);
        })
        .catch(() => showToast("設定の反映に失敗しました"));
    }, PATCH_DEBOUNCE_MS);
  };

  const save = async () => {
    setSaving(true);
    try {
      await api.saveSettings();
      setDirty(false);
      showToast("設定を保存しました");
    } catch {
      showToast("設定の保存に失敗しました");
    } finally {
      setSaving(false);
    }
  };

  return (
    <>
      <GlassButton className="status-settings-btn" aria-label="設定" title="設定" onClick={() => setOpen(true)}>
        <SettingsIcon />
      </GlassButton>
      {open && (
        <div className="modal-backdrop" onClick={() => setOpen(false)}>
          <div className="modal-panel" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <span>人感センサー設定</span>
              <button type="button" className="modal-close" aria-label="閉じる" onClick={() => setOpen(false)}>
                <CloseIcon />
              </button>
            </div>
            {!current ? (
              <p className="note">読み込み中...</p>
            ) : (
              <div className="modal-body">
                {FIELDS.map((f) => (
                  <div className="field" key={f.key}>
                    <div className="field-label">
                      <span>{f.label}</span>
                      <span>
                        {current[f.key]}
                        {f.suffix ?? ""}
                      </span>
                    </div>
                    <input
                      type="range"
                      min={f.min}
                      max={f.max}
                      step={f.step}
                      value={current[f.key]}
                      onChange={(e) => patch(f.key, Number(e.target.value))}
                    />
                  </div>
                ))}
                <GlassButton
                  className={`btn-sm tone-accent ${dirty ? "settings-save-highlight" : ""}`}
                  disabled={saving || !dirty}
                  onClick={save}
                >
                  保存
                </GlassButton>
              </div>
            )}
          </div>
        </div>
      )}
    </>
  );
}
