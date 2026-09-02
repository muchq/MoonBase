import { QueryClient } from '@tanstack/react-query';
import type {
  GameRow,
  IndexRequest,
  OccurrenceRow,
  QueryResponse,
  QueryStats,
  QueryTerms,
} from './types';

// re-export so consumers can import from one place
export type {
  GameRow,
  IndexRequest,
  OccurrenceRow,
  QueryResponse,
  QueryStats,
  QueryTerms,
};

const API_BASE = 'https://api.1d4.net';

interface ApiError extends Error {
  status?: number;
  body?: string | null;
}

// The API reports failures as {"error": string, ...} (see one_d4's ErrorHandler). Surface that
// string as the Error message so the UI shows "Expected a number..." instead of the raw JSON
// envelope; anything else (plain text, proxy HTML) falls through as-is.
function errorMessage(body: string | null): string | null {
  if (!body) return null;
  try {
    const parsed: unknown = JSON.parse(body);
    if (
      parsed !== null &&
      typeof parsed === 'object' &&
      typeof (parsed as { error?: unknown }).error === 'string'
    ) {
      return (parsed as { error: string }).error;
    }
  } catch (_) {
    // not JSON
  }
  return body;
}

// react-query retry policy: a 4xx is deterministic (the same bad query fails identically every
// time), so retrying only delays showing the error — ~7s under the default 3-retry backoff.
// Except 408 (timeout) and 429 (throttled), the two 4xx a retry can actually fix; those and
// everything else keep the default three attempts.
export function retryUnlessClientError(
  failureCount: number,
  error: unknown
): boolean {
  const status = (error as ApiError | null)?.status;
  if (
    typeof status === 'number' &&
    status >= 400 &&
    status < 500 &&
    status !== 408 &&
    status !== 429
  ) {
    return false;
  }
  return failureCount < 3;
}

// The QueryClient the app boots with. A factory rather than inline config in main.tsx so a test
// can pin that the retry policy is actually wired — main.tsx renders into #root and cannot be
// imported under vitest, and every component test builds its own client.
export function makeQueryClient(): QueryClient {
  return new QueryClient({
    defaultOptions: { queries: { retry: retryUnlessClientError } },
  });
}

async function request<T>(path: string, options: RequestInit = {}): Promise<T> {
  const url = `${API_BASE}${path}`;
  const res = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options.headers,
    },
  });
  if (!res.ok) {
    let body: string | null = null;
    try {
      body = await res.text();
    } catch (_) {
      // ignore
    }
    const err = new Error(errorMessage(body) || res.statusText) as ApiError;
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return res.json();
}

export async function createIndex(body: {
  player: string;
  platform: string;
  startMonth: string;
  endMonth: string;
  excludeBullet?: boolean;
}): Promise<IndexRequest> {
  return request('/v1/index', { method: 'POST', body: JSON.stringify(body) });
}

export async function listIndexRequests(): Promise<IndexRequest[]> {
  return request('/v1/index');
}

export async function getIndexStatus(id: string): Promise<IndexRequest> {
  return request(`/v1/index/${id}`);
}

export async function query(body: {
  query: string;
  limit: number;
  offset: number;
}): Promise<QueryResponse> {
  return request('/v1/query', { method: 'POST', body: JSON.stringify(body) });
}

export const STATS_WINDOW_DAYS = 30;

export async function getQueryStats(): Promise<QueryStats> {
  return request(`/stats/v1/one_d4/queries?days=${STATS_WINDOW_DAYS}`);
}

export async function getQueryTerms(): Promise<QueryTerms> {
  return request(`/stats/v1/one_d4/terms?days=${STATS_WINDOW_DAYS}&limit=200`);
}
