import type { GameRow, IndexRequest, OccurrenceRow, QueryResponse } from './types';

// re-export so consumers can import from one place
export type { GameRow, IndexRequest, OccurrenceRow, QueryResponse };

const API_BASE = 'https://api.1d4.net';

interface ApiError extends Error {
  status?: number;
  body?: string | null;
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
    const err = new Error(body || res.statusText) as ApiError;
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
  includeBullet?: boolean;
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
