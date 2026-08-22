import type { CSSProperties, ReactNode } from "react";
import GlassPanel from "./GlassPanel";

interface GlassButtonProps {
  children: ReactNode;
  onClick?: () => void;
  active?: boolean;
  disabled?: boolean;
  className?: string;
  style?: CSSProperties;
  cornerRadius?: number;
  "aria-label"?: string;
  title?: string;
}

export default function GlassButton({
  children,
  onClick,
  active = false,
  disabled = false,
  className = "",
  style,
  cornerRadius = 14,
  ...aria
}: GlassButtonProps) {
  const handleClick = disabled ? undefined : onClick;
  return (
    <GlassPanel
      card={false}
      cornerRadius={cornerRadius}
      className={`glass-btn ${active ? "is-active" : ""} ${disabled ? "is-disabled" : ""} ${className}`}
      style={style}
    >
      <div
        className="glass-btn-inner"
        role={handleClick ? "button" : undefined}
        tabIndex={handleClick ? 0 : undefined}
        aria-disabled={disabled || undefined}
        onClick={handleClick}
        onKeyDown={
          handleClick
            ? (e) => {
                if (e.key === "Enter" || e.key === " ") {
                  e.preventDefault();
                  handleClick();
                }
              }
            : undefined
        }
        {...aria}
      >
        {children}
      </div>
    </GlassPanel>
  );
}
