// Copies the production build (web/dist) into ../data/web, which is what
// `pio run --target uploadfs` flashes to the M5StampS3's LittleFS partition.
// Run via `npm run deploy` after any frontend change that should reach the device.
import { cpSync, rmSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const webDir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const distDir = path.join(webDir, "dist");
const targetDir = path.join(webDir, "..", "data", "web");

if (!existsSync(distDir)) {
  console.error("dist/ not found - run `npm run build` first");
  process.exit(1);
}

rmSync(targetDir, { recursive: true, force: true });
cpSync(distDir, targetDir, { recursive: true });
console.log(`Copied ${distDir} -> ${targetDir}`);
