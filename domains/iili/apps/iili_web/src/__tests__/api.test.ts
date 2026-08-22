import { afterEach, describe, expect, it, vi } from 'vitest';
import { SHORTEN_TIMEOUT_MS, shorten, shortLink } from '../api';

function mockFetch(status: number, body: string) {
  const fn = vi.fn().mockResolvedValue(
    new Response(body, { status, headers: { 'Content-Type': 'application/json' } })
  );
  vi.stubGlobal('fetch', fn);
  return fn;
}

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe('shorten', () => {
  it('POSTs longUrl and expiresAt as JSON and returns the slug', async () => {
    const timeoutSpy = vi.spyOn(AbortSignal, 'timeout');
    const fetchMock = mockFetch(201, '{"slug":"AQA"}');

    const result = await shorten('https://example.com/page', 1755003600000);

    expect(result).toEqual({ slug: 'AQA' });
    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe('https://api.muchq.com/iili/v1/shorten');
    expect(init.method).toBe('POST');
    // Without this header the browser sends text/plain and drops out of the
    // preflighted class the Caddy CORS allow-list serves.
    expect(init.headers).toMatchObject({ 'Content-Type': 'application/json' });
    // Pin the budget, not just "some AbortSignal" — 1ms or an hour would
    // still green an instanceof check.
    expect(SHORTEN_TIMEOUT_MS).toBe(10_000);
    expect(timeoutSpy).toHaveBeenCalledWith(10_000);
    expect(init.signal).toBeInstanceOf(AbortSignal);
    expect(JSON.parse(init.body)).toEqual({
      longUrl: 'https://example.com/page',
      expiresAt: 1755003600000,
    });
  });

  it('surfaces the fieldList message from a trait 400', async () => {
    mockFetch(
      400,
      JSON.stringify({
        fieldList: [{ message: 'Member must have length between 11 and 1000, inclusive', path: '/longUrl' }],
        message: '1 validation error detected. …',
      })
    );

    await expect(shorten('http://g.c', 1)).rejects.toThrow(
      'Member must have length between 11 and 1000, inclusive'
    );
  });

  it('surfaces the modeled message from a clock-rule 400', async () => {
    mockFetch(400, '{"message":"expiresAt is in the past"}');

    await expect(shorten('https://example.com', 1)).rejects.toThrow('expiresAt is in the past');
  });

  it('replaces a 429 with the friendly throttle message', async () => {
    mockFetch(429, '{"message":"whatever the server said"}');

    await expect(shorten('https://example.com', 1)).rejects.toThrow(
      'Slow down — too many links. Try again in a minute.'
    );
  });

  it('never renders an empty error for a bodyless gateway failure', async () => {
    mockFetch(502, '');

    await expect(shorten('https://example.com', 1)).rejects.toThrow(/something went wrong/i);
  });

  it('maps a network failure or timeout to a friendly message', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockRejectedValue(new DOMException('signal timed out', 'TimeoutError'))
    );

    await expect(shorten('https://example.com', 1)).rejects.toThrow(/network trouble/i);
  });
});

describe('shortLink', () => {
  it('mints i.iili.uk short links', () => {
    expect(shortLink('AQA')).toBe('https://i.iili.uk/r/AQA');
  });
});
