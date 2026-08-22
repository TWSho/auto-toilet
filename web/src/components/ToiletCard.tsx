import { useState } from "react";
import GlassPanel from "./GlassPanel";
import GlassButton from "./GlassButton";
import DetailToggle from "./DetailToggle";
import { DropletIcon } from "./Icons";
import { useBackend } from "../context/BackendContext";
import { useToiletAction } from "../hooks/useToiletAction";
import type { SprayState } from "../api";

const TEMP_LABELS = ["低", "中", "高"];

function FlushChip({
  id,
  label,
  run,
  disabled,
}: {
  id: string;
  label: string;
  run: (id: string) => Promise<boolean>;
  disabled: boolean;
}) {
  const [flashing, setFlashing] = useState(false);
  return (
    <GlassButton
      className="tone-accent"
      active={flashing}
      disabled={disabled}
      onClick={async () => {
        setFlashing(true);
        window.setTimeout(() => setFlashing(false), 900);
        await run(id);
      }}
    >
      {label}
    </GlassButton>
  );
}

function Dots({ level, max, compact }: { level: number; max: number; compact?: boolean }) {
  return (
    <div className={`dot-track ${compact ? "compact" : ""}`}>
      {Array.from({ length: max }, (_, i) => i + 1).map((i) => (
        <span key={i} className={`dot static ${i <= level ? "on" : ""}`} />
      ))}
    </div>
  );
}

export default function ToiletCard() {
  const [detailOpen, setDetailOpen] = useState(false);
  const { toilet } = useBackend();
  const { run, disabledFor } = useToiletAction();

  const spray: SprayState = toilet?.spray ?? "off";
  const waterPressure = toilet?.waterPressure ?? 3;
  const seatTemp = toilet?.seatTemp ?? 1;
  const waterTemp = toilet?.waterTemp ?? 1;
  const deodorizer = toilet?.deodorizer ?? false;
  const autoClean = toilet?.autoClean ?? false;

  const sprayTargetId = (value: Exclude<SprayState, "off">) => (spray === value ? "spray_off" : `spray_${value}`);

  return (
    <GlassPanel card cornerRadius={14} className="col-card toilet-card">
      <div className="card-body">
        <div className="toilet-topbar">
          <FlushChip id="flush_large" label="流す(大)" run={run} disabled={disabledFor("flush_large")} />
          <FlushChip id="flush_small" label="流す(小)" run={run} disabled={disabledFor("flush_small")} />
          <FlushChip id="flush_eco" label="流す(ECO)" run={run} disabled={disabledFor("flush_eco")} />
        </div>

        <div className="toilet-main">
          <div className="spray-row">
            <GlassButton
              className="wash-stop tone-warn"
              title="止"
              aria-label="止"
              disabled={disabledFor("spray_off")}
              onClick={() => run("spray_off")}
            >
              <span className="sq" />
            </GlassButton>

            <div className="spray-buttons">
              <div className="spray-btn" data-tone="blue">
                <GlassButton
                  active={spray === "rear"}
                  className="tone-accent"
                  disabled={disabledFor(sprayTargetId("rear"))}
                  onClick={() => run(sprayTargetId("rear"))}
                >
                  <span className="circle-icon">
                    <DropletIcon />
                  </span>
                  <span className="label">おしり</span>
                </GlassButton>
              </div>
              <div className="spray-btn" data-tone="blue">
                <GlassButton
                  active={spray === "soft"}
                  className="tone-accent"
                  disabled={disabledFor(sprayTargetId("soft"))}
                  onClick={() => run(sprayTargetId("soft"))}
                >
                  <span className="circle-icon">
                    <DropletIcon />
                  </span>
                  <span className="label">やわらか</span>
                </GlassButton>
              </div>
              <div className="spray-btn" data-tone="red">
                <GlassButton
                  active={spray === "bidet"}
                  className="tone-danger"
                  disabled={disabledFor(sprayTargetId("bidet"))}
                  onClick={() => run(sprayTargetId("bidet"))}
                >
                  <span className="circle-icon">
                    <DropletIcon />
                  </span>
                  <span className="label">ビデ</span>
                </GlassButton>
              </div>
            </div>
          </div>

          <div className="toilet-section">
            <div className="ctrl-row">
              <div className="toilet-section-title">水勢</div>
              <div className="ctrl-right">
                <GlassButton
                  className="level-end"
                  disabled={disabledFor("water_pressure_down")}
                  onClick={() => run("water_pressure_down")}
                >
                  弱
                </GlassButton>
                <Dots level={waterPressure} max={5} />
                <GlassButton
                  className="level-end"
                  disabled={disabledFor("water_pressure_up")}
                  onClick={() => run("water_pressure_up")}
                >
                  強
                </GlassButton>
              </div>
            </div>
            <div className="ctrl-row">
              <div className="toilet-section-title">洗浄位置</div>
              <div className="ctrl-right nozzle-row">
                <GlassButton
                  className="level-end"
                  disabled={disabledFor("nozzle_position_forward")}
                  onClick={() => run("nozzle_position_forward")}
                >
                  前
                </GlassButton>
                <GlassButton
                  className="level-end"
                  disabled={disabledFor("nozzle_position_backward")}
                  onClick={() => run("nozzle_position_backward")}
                >
                  後
                </GlassButton>
              </div>
            </div>
          </div>

          <div className="toilet-section">
            <div className="ctrl-row">
              <div className="toilet-section-title">便座温度</div>
              <div className="ctrl-right">
                <Dots level={seatTemp} max={3} compact />
                <GlassButton
                  className="level-end tone-accent"
                  disabled={disabledFor("seat_temp_cycle")}
                  onClick={() => run("seat_temp_cycle")}
                >
                  {TEMP_LABELS[seatTemp - 1] ?? "―"}
                </GlassButton>
              </div>
            </div>
            <div className="ctrl-row">
              <div className="toilet-section-title">温水温度</div>
              <div className="ctrl-right">
                <Dots level={waterTemp} max={3} compact />
                <GlassButton
                  className="level-end tone-accent"
                  disabled={disabledFor("water_temp_cycle")}
                  onClick={() => run("water_temp_cycle")}
                >
                  {TEMP_LABELS[waterTemp - 1] ?? "―"}
                </GlassButton>
              </div>
            </div>
          </div>
        </div>

        <DetailToggle open={detailOpen} onToggle={() => setDetailOpen((v) => !v)} />
        {detailOpen && (
          <div className="detail-panel">
            <div className="toggle-row">
              <span>パワー脱臭</span>
              <GlassButton
                className="pill-toggle tone-accent"
                active={deodorizer}
                disabled={disabledFor(deodorizer ? "deodorizer_off" : "deodorizer_on")}
                onClick={() => run(deodorizer ? "deodorizer_off" : "deodorizer_on")}
              >
                {deodorizer ? "入" : "切"}
              </GlassButton>
            </div>
            <div className="toggle-row">
              <span>ノズルそうじ</span>
              <GlassButton className="btn-sm tone-accent" disabled={disabledFor("nozzle_clean")} onClick={() => run("nozzle_clean")}>
                実行
              </GlassButton>
            </div>
            <div className="toggle-row">
              <span>オート洗浄</span>
              <GlassButton
                className="pill-toggle tone-accent"
                active={autoClean}
                disabled={disabledFor(autoClean ? "auto_clean_off" : "auto_clean_on")}
                onClick={() => run(autoClean ? "auto_clean_off" : "auto_clean_on")}
              >
                {autoClean ? "入" : "切"}
              </GlassButton>
            </div>
          </div>
        )}
      </div>
    </GlassPanel>
  );
}
