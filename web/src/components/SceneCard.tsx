import { useState } from "react";
import GlassPanel from "./GlassPanel";
import GlassButton from "./GlassButton";
import DetailToggle from "./DetailToggle";
import { useBackend } from "../context/BackendContext";
import type { SceneApi } from "../api";

function SceneTile({
  scene,
  trackTitle,
  active,
  onClick,
}: {
  scene: SceneApi;
  trackTitle: string;
  active: boolean;
  onClick: () => void;
}) {
  return (
    <GlassButton className="scene-card tone-accent" active={active} onClick={onClick}>
      <span className="scene-swatch" style={{ background: scene.led.color }} />
      <div>
        <div className="scene-name">{scene.name}</div>
        <div className="scene-desc">
          {scene.led.brightness}% ・ {trackTitle}
        </div>
      </div>
    </GlassButton>
  );
}

export default function SceneCard() {
  const { scenes, tracks, executeScene, createScene } = useBackend();
  const [activeSceneId, setActiveSceneId] = useState<string | null>(null);
  const [detailOpen, setDetailOpen] = useState(false);
  const [newName, setNewName] = useState("");
  const [newColor, setNewColor] = useState("#8ec9ff");
  const [newBright, setNewBright] = useState(70);
  const [newTrackId, setNewTrackId] = useState("");
  const [saving, setSaving] = useState(false);

  const trackTitle = (id: string) => tracks.find((t) => t.id === id)?.title ?? "";

  const applyScene = (id: string) => {
    executeScene(id);
    setActiveSceneId(id);
  };

  const saveScene = async () => {
    const name = newName.trim();
    if (!name) {
      window.alert("シーン名を入力してください");
      return;
    }
    const trackId = newTrackId || tracks[0]?.id;
    if (!trackId) return;
    setSaving(true);
    const ok = await createScene({ name, led: { brightness: newBright, color: newColor }, bgmTrackId: trackId });
    setSaving(false);
    if (ok) setNewName("");
  };

  const primaryScenes = scenes.filter((s) => s.primary);
  const extraScenes = scenes.filter((s) => !s.primary);

  return (
    <GlassPanel card cornerRadius={14} className="col-card scene-panel">
      <div className="card-body">
        <div className="primary-zone">
          {primaryScenes.map((s) => (
            <SceneTile
              key={s.id}
              scene={s}
              trackTitle={trackTitle(s.bgmTrackId)}
              active={activeSceneId === s.id}
              onClick={() => applyScene(s.id)}
            />
          ))}
          {extraScenes.length > 0 && (
            <div className="field">
              <div className="field-label">
                <span>その他のシーン</span>
              </div>
              <div className="scene-list-extra">
                {extraScenes.map((s) => (
                  <SceneTile
                    key={s.id}
                    scene={s}
                    trackTitle={trackTitle(s.bgmTrackId)}
                    active={activeSceneId === s.id}
                    onClick={() => applyScene(s.id)}
                  />
                ))}
              </div>
            </div>
          )}
        </div>

        <DetailToggle open={detailOpen} onToggle={() => setDetailOpen((v) => !v)} />
        {detailOpen && (
          <div className="detail-panel">
            <div className="field">
              <div className="field-label">
                <span>新規シーン作成</span>
              </div>
              <input
                className="mini-input"
                placeholder="シーン名"
                value={newName}
                onChange={(e) => setNewName(e.target.value)}
              />
              <input type="color" value={newColor} onChange={(e) => setNewColor(e.target.value)} />
              <div className="field-label">
                <span>明るさ</span>
                <span>{newBright}%</span>
              </div>
              <input
                type="range"
                min={0}
                max={100}
                value={newBright}
                onChange={(e) => setNewBright(Number(e.target.value))}
              />
              <select
                className="mini-input"
                value={newTrackId || tracks[0]?.id || ""}
                onChange={(e) => setNewTrackId(e.target.value)}
              >
                {tracks.map((t) => (
                  <option key={t.id} value={t.id}>
                    {t.title}
                  </option>
                ))}
              </select>
              <GlassButton className="btn-sm tone-accent" style={{ marginTop: 5 }} disabled={saving} onClick={saveScene}>
                保存
              </GlassButton>
            </div>
          </div>
        )}
      </div>
    </GlassPanel>
  );
}
