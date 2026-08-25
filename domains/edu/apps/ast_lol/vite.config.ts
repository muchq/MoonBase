import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import astlolContent from './vite-plugin-content.ts';

export default defineConfig(async () => {
  const plugins = [tailwindcss(), react(), astlolContent()];

  // Cloudflare Workers plugin for dev/preview; skip in test environment to
  // avoid workerd initialisation in jsdom.
  if (!process.env.VITEST) {
    const { cloudflare } = await import('@cloudflare/vite-plugin');
    plugins.unshift(cloudflare());
  }

  return {
    plugins,
    test: {
      environment: 'jsdom',
      setupFiles: ['./src/setupTests.ts'],
      clearMocks: true,
    },
  };
});
