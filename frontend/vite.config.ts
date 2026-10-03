import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { mockApi } from "./dev/mock-api.mjs";

export default defineConfig(({ mode }) => ({
  plugins: [react(), ...(mode === "mock" ? [mockApi()] : [])],
  server: {
    port: 5173,
    strictPort: true,
    proxy: mode === "mock" ? undefined : {
      "/api": "http://192.0.2.1"
    }
  },
  build: {
    target: "es2022",
    sourcemap: false,
    rollupOptions: {
      output: {
        entryFileNames: "assets/app.js",
        chunkFileNames: "assets/[name].js",
        assetFileNames: "assets/[name][extname]"
      }
    }
  }
}));
