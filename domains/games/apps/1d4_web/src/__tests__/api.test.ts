import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import {
  createIndex,
  getQueryStats,
  getQueryTerms,
  listIndexRequests,
  query,
  retryUnlessClientError,
} from '../api';

describe('api', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  function mockFetch(data: unknown, ok = true, status = 200) {
    const fetchMock = vi.fn().mockResolvedValue({
      ok,
      status,
      statusText: ok ? 'OK' : 'Error',
      json: () => Promise.resolve(data),
      text: () => Promise.resolve(ok ? '' : JSON.stringify(data)),
    });
    vi.stubGlobal('fetch', fetchMock);
    return fetchMock;
  }

  describe('createIndex', () => {
    it('sends POST to /v1/index with correct body', async () => {
      const body = {
        player: 'hikaru',
        platform: 'CHESS_COM',
        startMonth: '2024-01',
        endMonth: '2024-03',
        excludeBullet: true,
      };
      const mock = mockFetch({ id: '123', status: 'PENDING', gamesIndexed: 0, errorMessage: null });
      await createIndex(body);
      expect(mock).toHaveBeenCalledWith(
        'https://api.1d4.net/v1/index',
        expect.objectContaining({ method: 'POST' })
      );
      const call = mock.mock.calls[0][1] as RequestInit;
      expect(JSON.parse(call.body as string)).toEqual(body);
    });
  });

  // The stats tab reads through api.1d4.net because that host's CORS grant
  // covers 1d4.net; Caddy proxies exactly GET /stats/v1/one_d4/* there, so
  // the path is a contract with the Caddyfile (deploy_config_test pins the
  // other side).
  describe('query stats', () => {
    it('sends GET to /stats/v1/one_d4/queries and /terms for the window', async () => {
      const mock = mockFetch({ days: 30, rows: [] });
      await getQueryStats();
      await getQueryTerms();
      expect(mock.mock.calls.map((call) => call[0])).toEqual([
        'https://api.1d4.net/stats/v1/one_d4/queries?days=30',
        'https://api.1d4.net/stats/v1/one_d4/terms?days=30&limit=200',
      ]);
    });
  });

  describe('listIndexRequests', () => {
    it('sends GET to /v1/index', async () => {
      const mock = mockFetch([]);
      await listIndexRequests();
      expect(mock).toHaveBeenCalledWith(
        'https://api.1d4.net/v1/index',
        expect.any(Object)
      );
      const call = mock.mock.calls[0][1] as RequestInit;
      expect(call.method).toBeUndefined(); // GET by default
    });
  });

  describe('query', () => {
    it('sends POST to /v1/query', async () => {
      const mock = mockFetch({ games: [], count: 0 });
      await query({ query: 'motif(fork)', limit: 50, offset: 0 });
      expect(mock).toHaveBeenCalledWith(
        'https://api.1d4.net/v1/query',
        expect.objectContaining({ method: 'POST' })
      );
    });
  });

  describe('error handling', () => {
    it('throws an error with body text on non-ok response', async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: false,
          status: 400,
          statusText: 'Bad Request',
          text: () => Promise.resolve('invalid query syntax'),
        })
      );
      await expect(listIndexRequests()).rejects.toThrow('invalid query syntax');
    });

    it('falls back to statusText when body is empty', async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: false,
          status: 500,
          statusText: 'Internal Server Error',
          text: () => Promise.resolve(''),
        })
      );
      await expect(listIndexRequests()).rejects.toThrow(
        'Internal Server Error'
      );
    });

    it('unwraps the API JSON error envelope so the message is the error, not the JSON', async () => {
      const body =
        '{"error":"ChessQL has no NULL literal at position 12","position":12}';
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: false,
          status: 400,
          statusText: 'Bad Request',
          text: () => Promise.resolve(body),
        })
      );
      const err = (await query({
        query: 'played.at = NULL',
        limit: 50,
        offset: 0,
      }).catch((e: unknown) => e)) as Error & {
        status?: number;
        body?: string | null;
      };
      expect(err.message).toBe('ChessQL has no NULL literal at position 12');
      expect(err.status).toBe(400);
      expect(err.body).toBe(body);
    });

    it('keeps a non-envelope JSON body as raw text', async () => {
      vi.stubGlobal(
        'fetch',
        vi.fn().mockResolvedValue({
          ok: false,
          status: 502,
          statusText: 'Bad Gateway',
          text: () => Promise.resolve('{"message":"upstream down"}'),
        })
      );
      await expect(listIndexRequests()).rejects.toThrow(
        '{"message":"upstream down"}'
      );
    });
  });

  describe('retryUnlessClientError', () => {
    // A 4xx is deterministic — the same bad query fails the same way every time, and
    // react-query's default 3 retries with backoff only delay showing the error by ~7s.
    it('does not retry 4xx responses', () => {
      const err = Object.assign(new Error('bad request'), { status: 400 });
      expect(retryUnlessClientError(0, err)).toBe(false);
    });

    it('retries 5xx responses up to 3 times', () => {
      const err = Object.assign(new Error('server error'), { status: 503 });
      expect(retryUnlessClientError(0, err)).toBe(true);
      expect(retryUnlessClientError(2, err)).toBe(true);
      expect(retryUnlessClientError(3, err)).toBe(false);
    });

    it('retries errors with no status (network failures)', () => {
      expect(retryUnlessClientError(0, new Error('network'))).toBe(true);
      expect(retryUnlessClientError(3, new Error('network'))).toBe(false);
    });

    it('still retries 408 and 429 — the two 4xx that are not deterministic', () => {
      const timeout = Object.assign(new Error('timeout'), { status: 408 });
      const throttled = Object.assign(new Error('throttled'), { status: 429 });
      expect(retryUnlessClientError(0, timeout)).toBe(true);
      expect(retryUnlessClientError(0, throttled)).toBe(true);
      expect(retryUnlessClientError(3, throttled)).toBe(false);
    });
  });

  describe('makeQueryClient', () => {
    it('wires retryUnlessClientError as the default query retry', async () => {
      // main.tsx cannot be imported under vitest (it renders into #root), so the wiring lives in
      // this factory — otherwise deleting the defaultOptions line would leave every test green
      // while restoring the ~7s retry delay on 4xx.
      const { makeQueryClient } = await import('../api');
      expect(makeQueryClient().getDefaultOptions().queries?.retry).toBe(
        retryUnlessClientError
      );
    });
  });
});
