import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The build is copied into the shell binary and served from liora://ui/.
// Relative asset URLs keep it working from any origin.
export default defineConfig({
  plugins: [svelte()],
  base: './',
  build: {
    target: 'safari16',
    outDir: 'dist',
    emptyOutDir: true,
    assetsInlineLimit: 4096,
    cssCodeSplit: false,
    reportCompressedSize: false,
  },
  server: {
    port: 5173,
  },
});
