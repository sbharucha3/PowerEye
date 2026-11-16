import path from "node:path";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

// The LoRa hub serves its JSON API from its own Wi-Fi access point. Browsers on
// localhost cannot call it directly (CORS), so the dev server proxies /api.
// Override the target with HUB_URL=http://host vite if the hub address changes.
const HUB = process.env.HUB_URL ?? "http://192.168.4.1";

export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: { alias: { "@": path.resolve(import.meta.dirname, "src") } },
  server: {
    port: 5173,
    proxy: { "/api": { target: HUB, changeOrigin: true, timeout: 4000, proxyTimeout: 4000 } },
  },
});
