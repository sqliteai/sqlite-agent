import { defineConfig } from 'tsup';

export default defineConfig({
  entry: ['src/index.ts'],
  format: ['cjs', 'esm'],
  // Defines import.meta.url in the CJS output, so the createRequire fallback in
  // src/index.ts compiles cleanly for both formats.
  shims: true,
  dts: true,
  splitting: false,
  sourcemap: true,
  clean: true,
  treeshake: true,
  minify: false,
  target: 'node16',
  outDir: 'dist',
});
