import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
export default defineConfig({
  root: 'web', plugins: [react()], envDir: '..',
  build: { outDir: '../dist', emptyOutDir: true },
  server: { port: 5173, strictPort: true },
});
