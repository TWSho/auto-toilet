import { useState } from "react";
import GlassPanel from "./GlassPanel";
import GlassButton from "./GlassButton";
import DetailToggle from "./DetailToggle";
import {
  MusicIcon,
  PlayIcon,
  PauseIcon,
  SkipBackIcon,
  SkipForwardIcon,
  RepeatIcon,
  ShuffleIcon,
} from "./Icons";
import { useBackend } from "../context/BackendContext";

function formatTime(totalSeconds: number) {
  const m = Math.floor(totalSeconds / 60);
  const s = String(Math.floor(totalSeconds % 60)).padStart(2, "0");
  return `${m}:${s}`;
}

export default function BgmCard() {
  const [detailOpen, setDetailOpen] = useState(false);
  const { bgm, tracks, updateBgm, bgmNext, bgmPrevious } = useBackend();

  const playing = bgm?.playing ?? false;
  const volume = bgm?.volume ?? 0;
  const elapsedSec = bgm?.elapsedSec ?? 0;
  const durationSec = bgm?.durationSec ?? 0;
  const repeat = bgm?.repeat ?? "off";
  const shuffle = bgm?.shuffle ?? false;
  const track = tracks.find((t) => t.id === bgm?.trackId) ?? tracks[0];

  return (
    <GlassPanel card cornerRadius={14} className="col-card bgm-panel">
      <div className="card-body">
        <div className="primary-zone">
          <div className="now-playing">
            <div className="art">
              <MusicIcon />
            </div>
            <div className="meta">
              <div className="title">{track?.title ?? "―"}</div>
            </div>
          </div>

          <GlassButton className="btn-lg tone-accent" onClick={() => updateBgm({ action: playing ? "pause" : "play" })}>
            {playing ? <PauseIcon /> : <PlayIcon />}
            <span>{playing ? "一時停止" : "再生"}</span>
          </GlassButton>

          <div className="field">
            <div className="field-label">
              <span>音量</span>
              <span>{volume}%</span>
            </div>
            <input
              type="range"
              min={0}
              max={100}
              value={volume}
              onChange={(e) => updateBgm({ volume: Number(e.target.value) })}
            />
          </div>
        </div>

        <DetailToggle open={detailOpen} onToggle={() => setDetailOpen((v) => !v)} />
        {detailOpen && (
          <div className="detail-panel">
            <div className="transport-row">
              <GlassButton title="前の曲" onClick={bgmPrevious}>
                <SkipBackIcon />
              </GlassButton>
              <GlassButton
                title="リピート"
                active={repeat === "one"}
                className="tone-accent"
                onClick={() => updateBgm({ repeat: repeat === "one" ? "off" : "one" })}
              >
                <RepeatIcon />
              </GlassButton>
              <GlassButton
                title="シャッフル"
                active={shuffle}
                className="tone-accent"
                onClick={() => updateBgm({ shuffle: !shuffle })}
              >
                <ShuffleIcon />
              </GlassButton>
              <GlassButton title="次の曲" onClick={bgmNext}>
                <SkipForwardIcon />
              </GlassButton>
            </div>
            <div className="field">
              <div className="field-label">
                <span>再生位置</span>
                <span>
                  {formatTime(elapsedSec)} / {formatTime(durationSec)}
                </span>
              </div>
              <input
                type="range"
                min={0}
                max={durationSec || 1}
                value={elapsedSec}
                onChange={(e) => updateBgm({ seekSec: Number(e.target.value) })}
              />
            </div>
            <div className="field">
              <div className="field-label">
                <span>選曲</span>
              </div>
              <div className="song-list">
                {tracks.map((t) => (
                  <div
                    key={t.id}
                    className={`song-item ${t.id === bgm?.trackId ? "current" : ""}`}
                    onClick={() => updateBgm({ trackId: t.id })}
                  >
                    <span>{t.title}</span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        )}
      </div>
    </GlassPanel>
  );
}
