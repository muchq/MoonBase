// @vitest-environment node
import { describe, expect, it, vi } from 'vitest';
import worker from '../../worker/index.js';
import { shortLink } from '../api';

function makeEnv() {
  // A fresh Response per call: bodies are single-read.
  return { ASSETS: { fetch: vi.fn(async () => new Response('asset')) } };
}

describe('worker', () => {
  it('302s GET /r/{slug} to the v2 API redirect route', async () => {
    const env = makeEnv();
    const response = await worker.fetch(new Request('https://r3dr.net/r/AQA'), env);

    expect(response.status).toBe(302);
    expect(response.headers.get('Location')).toBe('https://api.muchq.com/r3dr/v2/r/AQA');
    expect(env.ASSETS.fetch).not.toHaveBeenCalled();
  });

  it('handles exactly the links the SPA mints', async () => {
    const env = makeEnv();
    const response = await worker.fetch(new Request(shortLink('AQA')), env);
    expect(response.status).toBe(302);
  });

  it('gives HEAD the same answer as GET — unfurlers and probes see the redirect', async () => {
    const env = makeEnv();
    const response = await worker.fetch(
      new Request('https://r3dr.net/r/AQA', { method: 'HEAD' }),
      env
    );
    expect(response.status).toBe(302);
    expect(env.ASSETS.fetch).not.toHaveBeenCalled();
  });

  it('tolerates a trailing slash', async () => {
    const env = makeEnv();
    const response = await worker.fetch(new Request('https://r3dr.net/r/AQA/'), env);
    expect(response.status).toBe(302);
    expect(response.headers.get('Location')).toBe('https://api.muchq.com/r3dr/v2/r/AQA');
  });

  it('serves everything else from assets', async () => {
    const env = makeEnv();
    for (const url of [
      'https://r3dr.net/',
      'https://r3dr.net/index.html',
      'https://r3dr.net/r/', // no slug
      'https://r3dr.net/r/AQA/extra', // not a slug path
    ]) {
      const response = await worker.fetch(new Request(url), env);
      expect(await response.text()).toBe('asset');
    }
    expect(env.ASSETS.fetch).toHaveBeenCalledTimes(4);
  });

  it('leaves non-GET /r/ requests to the SPA fallback, not the API', async () => {
    const env = makeEnv();
    await worker.fetch(new Request('https://r3dr.net/r/AQA', { method: 'POST' }), env);
    expect(env.ASSETS.fetch).toHaveBeenCalledTimes(1);
  });
});
