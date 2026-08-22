// Frontend-only display data (swatch colors for the LED preset picker).
// The presetId values must match the backend's preset table (WebUIバックエンド仕様.md).
export const LED_PRESETS: { id: string; color: string; name: string }[] = [
  { id: "warm", color: "#fff3d6", name: "電球色" },
  { id: "neutral", color: "#ffffff", name: "昼白色" },
  { id: "night", color: "#cfeeff", name: "常夜灯" },
  { id: "pink", color: "#ffd1e8", name: "ピンク" },
];
