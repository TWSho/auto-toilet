import GlassPanel from "./GlassPanel";
import SettingsPanel from "./SettingsPanel";
import FullscreenButton from "./FullscreenButton";
import { USE_MOCK } from "../api";
import type { RoomState } from "../api";
import { useBackend } from "../context/BackendContext";

const ROOM_STATE_LABEL: Record<RoomState, string> = {
  VACANT: "不在",
  ENTERING: "入室中",
  STAYING: "滞在中",
};

export default function StatusCard() {
  const { online, roomState } = useBackend();

  const modeClass = USE_MOCK ? "is-mock" : online ? "" : "is-offline";
  const modeLabel = USE_MOCK ? "モックモード" : online ? "接続中" : "未接続";

  return (
    <GlassPanel card cornerRadius={14} className="col-card status-card">
      <div className="card-body status-card-body">
        <div className="status-info">
          <span className={`status-pill ${modeClass}`} title={USE_MOCK ? "モックデータで動作中（実機には接続していません）" : undefined}>
            <span className="status-dot" />
            {modeLabel}
          </span>
          <span className="status-pill">{roomState ? ROOM_STATE_LABEL[roomState] : "―"}</span>
        </div>
        <FullscreenButton />
        <SettingsPanel />
      </div>
    </GlassPanel>
  );
}
