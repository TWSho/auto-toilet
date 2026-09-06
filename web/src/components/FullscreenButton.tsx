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

  const enterFullscreen = () => {
    if (!document.fullscreenElement) {
      document.documentElement.requestFullscreen?.().catch(() => {});
    }
  };

  const toggle = () => {
    if (document.fullscreenElement) {
      document.exitFullscreen?.().catch(() => {});
    } else {
      enterFullscreen();
    }
  };

  // タブレット常設用ショートカット: マイコンからのUSB/BLE HIDキーボード入力
  // ("F"キー)で全画面表示に復帰させる(ロック画面解除操作で全画面が
  // 解除されてしまうため)。入力欄にフォーカスがある間は無視する。
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key.toLowerCase() !== "f" || e.ctrlKey || e.altKey || e.metaKey) return;
      const target = e.target as HTMLElement | null;
      const tag = target?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA" || target?.isContentEditable) return;
      enterFullscreen();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

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
