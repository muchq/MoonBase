import { afterEach, describe, expect, it, vi } from 'vitest';
import { shorten, shortLink, type ApiError } from '../api';

function mockFetch(status: number, body: string) {
  const fn = vi.fn().mockResolvedValue(
    new Response(body, { status, headers: { 'Content-Type': 'application/json' } })
  );
  vi.stubGlobal('fetch', fn);
  return fn;
}

afterEach(() => vi.unstubAllGlobals());

describe('shorten', () => {
  it('POSTs longUrl and expiresAt and returns the slug', async () => {
    const fetchMock = mockFetch(201, '{"slug":"AQA"}');

    const result = await shorten('https://example.com/page', 1755003600000);

    expect(result).toEqual({ slug: 'AQA' });
    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe('https://api.muchq.com/r3dr/v1/shorten');
    expect(init.method).toBe('POST');
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

  it('attaches the status for callers that branch on it', async () => {
    mockFetch(500, 'oops');

    await expect(shorten('https://example.com', 1)).rejects.toMatchObject({
      status: 500,
      message: 'oops',
    } satisfies Partial<ApiError>);
  });
});

describe('shortLink', () => {
  it('mints v1-shaped r3dr.net links', () => {
    expect(shortLink('AQA')).toBe('https://r3dr.net/r/AQA');
  });
});
