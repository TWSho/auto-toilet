import { defineConfig, loadEnv } from "vite";
import react from "@vitejs/plugin-react";

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  // In dev (`npm run dev`), the app is served from localhost, not the M5StampS3,
  // so /api requests need to be proxied to the real device on the LAN.
  // Set VITE_DEVICE_HOST (e.g. "192.168.11.42") to point at it; without it the
  // proxy target falls back to a placeholder that will simply fail to connect.
  const deviceHost = env.VITE_DEVICE_HOST || "192.168.11.42";

  return {
    plugins: [react()],
    server: {
      proxy: {
        "/api": {
          target: `http://${deviceHost}`,
          changeOrigin: true,
        },
      },
    },
    build: {
      // keep the production bundle small and flat - it ships inside the
      // ESP32's LittleFS partition (~1.5MB total, shared with scenes.json).
      assetsDir: "assets",
    },
  };
});
