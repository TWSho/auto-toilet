import type { CSSProperties, ReactNode } from "react";

interface GlassPanelProps {
  children: ReactNode;
  className?: string;
  cornerRadius?: number;
  /** true = fills the parent (grid/flex cell) with an internal scroll region.
   *  false = shrink-wraps to its content, e.g. buttons/chips/cards in a list. */
  card?: boolean;
  style?: CSSProperties;
}

/**
 * A plain CSS approximation of iOS 26 "Liquid Glass" (backdrop-filter blur +
 * saturation, a soft top-light sheen, a hairline border). No SVG displacement
 * filter, no per-instance mouse tracking - just a translucent surface, which
 * is what actually renders cheaply for a few dozen on-screen controls.
 */
export default function GlassPanel({
  children,
  className = "",
  cornerRadius = 21,
  card = false,
  style,
}: GlassPanelProps) {
  return (
    <div
      className={`glass-slot ${card ? "glass-slot--card" : "glass-slot--auto"} ${className}`}
      style={{ ...style, borderRadius: cornerRadius }}
    >
      <div className="glass-slot-content">{children}</div>
    </div>
  );
}
