// @vitest-environment node
import { describe, expect, it, vi } from 'vitest';
import worker from '../../worker/index.js';

function makeEnv() {
  // A fresh Response per call: bodies are single-read.
  return { ASSETS: { fetch: vi.fn(async () => new Response('asset')) } };
}

describe('worker', () => {
  it('serves every path from assets — redirects live on i.iili.uk, not here', async () => {
    const env = makeEnv();
    for (const url of [
      'https://iili.uk/',
      'https://iili.uk/index.html',
      'https://iili.uk/r/AQA', // short links are Caddy's job now
      'https://iili.uk/r/',
    ]) {
      const response = await worker.fetch(new Request(url), env);
      expect(await response.text()).toBe('asset');
    }
    expect(env.ASSETS.fetch).toHaveBeenCalledTimes(4);
  });
});
