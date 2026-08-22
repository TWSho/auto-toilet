import { useEffect, useState } from "react";
import GlassButton from "./GlassButton";
import { EnterFullscreenIcon, ExitFullscreenIcon } from "./Icons";

export default function FullscreenButton() {
  const [isFullscreen, setIsFullscreen] = useState(() => !!document.fullscreenElement);

  useEffect(() => {
    const onChange = () => setIsFullscreen(!!document.fullscreenElement);
    document.addEventListener("fullscreenchange", onChange);
    return () => document.removeEventListener("fullscreenchange", onChange);
  }, []);

  const toggle = () => {
    if (document.fullscreenElement) {
      document.exitFullscreen?.().catch(() => {});
    } else {
      document.documentElement.requestFullscreen?.().catch(() => {});
    }
  };

  return (
    <GlassButton
      className="status-fullscreen-btn"
      aria-label={isFullscreen ? "全画面を終了" : "全画面表示"}
      title={isFullscreen ? "全画面を終了" : "全画面表示"}
      onClick={toggle}
    >
      {isFullscreen ? <ExitFullscreenIcon /> : <EnterFullscreenIcon />}
    </GlassButton>
  );
}
