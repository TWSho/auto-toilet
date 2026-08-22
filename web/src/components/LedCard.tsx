import { useState } from "react";
import GlassPanel from "./GlassPanel";
import DetailToggle from "./DetailToggle";
import { LED_PRESETS } from "../types";
import { useBackend } from "../context/BackendContext";

export default function LedCard() {
  const [detailOpen, setDetailOpen] = useState(false);
  const { led, updateLed } = useBackend();

  const brightness = led?.brightness ?? 0;
  const color = led?.color ?? "#fff3d6";
  const presetId = led?.presetId ?? null;

  return (
    <GlassPanel card cornerRadius={14} className="col-card led-panel">
      <div className="card-body">
        <div className="primary-zone">
          <div className={`led-preview ${brightness > 0 ? "is-lit" : ""}`}>
            <div className="led-preview-fill" style={{ width: `${brightness}%`, background: color }} />
            <span className="led-preview-text">
              {brightness > 0 ? "点灯中" : "消灯"}
              <span className="led-preview-pct">{brightness}%</span>
            </span>
            <input
              type="range"
              className="led-preview-range"
              min={0}
              max={100}
              value={brightness}
              onChange={(e) => updateLed({ brightness: Number(e.target.value) })}
              aria-label="明るさ"
            />
          </div>

          <div className="field">
            <div className="field-label">
              <span>よく使う色</span>
            </div>
            <div className="swatches">
              {LED_PRESETS.map((preset) => (
                <span
                  key={preset.id}
                  className={`swatch ${presetId === preset.id ? "selected" : ""}`}
                  style={{ background: preset.color }}
                  title={preset.name}
                  onClick={() => updateLed({ presetId: preset.id, brightness: brightness > 0 ? brightness : 70 })}
                />
              ))}
            </div>
          </div>
        </div>

        <DetailToggle open={detailOpen} onToggle={() => setDetailOpen((v) => !v)} />
        {detailOpen && (
          <div className="detail-panel">
            <div className="field">
              <div className="field-label">
                <span>カスタムカラー</span>
              </div>
              <input
                type="color"
                value={color}
                onChange={(e) => updateLed({ color: e.target.value, brightness: brightness > 0 ? brightness : 70 })}
              />
            </div>
          </div>
        )}
      </div>
    </GlassPanel>
  );
}
