import { defineConfig, loadEnv } from "vite";
import react from "@vitejs/plugin-react";
import { mockApi } from "./dev/mock-api.mjs";

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  const apiTarget = env.API_TARGET;

  return {
    plugins: [react(), ...(mode === "mock" ? [mockApi()] : [])],
    server: {
      port: 5173,
      strictPort: true,
      proxy: mode === "mock" || !apiTarget ? undefined : {
        "/api": apiTarget
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
  };
});
