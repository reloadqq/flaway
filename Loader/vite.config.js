import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig(({ mode }) => ({
  plugins: [react()],

  // the app is always served from the document root (Neutralino),
  // so absolute paths keep public assets resolvable from CSS too
  base: "/",

  esbuild: {
    legalComments: "none",
    // no console/debugger output at all in release builds
    ...(mode === "production"
      ? { drop: ["console", "debugger"] }
      : { drop: ["debugger"] })
  },

  build: {
    // Neutralino packs this folder into resources.neu
    outDir: "resources",
    emptyOutDir: true,
    target: "es2020",
    minify: "esbuild",
    sourcemap: false,
    reportCompressedSize: false,
    chunkSizeWarningLimit: 800,
    cssCodeSplit: false,
    assetsInlineLimit: 4096,
    rollupOptions: {
      output: {
        manualChunks: undefined
      }
    }
  },

  server: {
    port: 5183,
    strictPort: false
  }
}));
