import { defineConfig } from "vite";
import { resolve } from "path";

export default defineConfig({
  root: resolve(__dirname, "."),
  base: "/send/",
  build: {
    outDir: resolve(__dirname, "../dist/public/send"),
    emptyOutDir: true,
    target: "esnext"
  }
});
