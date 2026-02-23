/**
 * API client for api.1d4.net — fetch wrappers.
 */

const API_BASE = 'https://api.1d4.net';

async function request(path, options = {}) {
  const url = `${API_BASE}${path}`;
  const res = await fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options.headers,
    },
  });
  if (!res.ok) {
    let body = null;
    try {
      body = await res.text();
    } catch (_) {}
    const err = new Error(body || res.statusText);
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return res.json();
}

/**
 * POST /v1/index — enqueue an index request.
 * @param {{ player: string, platform: string, startMonth: string, endMonth: string }} body
 * @returns {Promise<{ id: string, status: string, gamesIndexed: number, errorMessage: string|null }>}
 */
export async function createIndex(body) {
  return request('/v1/index', {
    method: 'POST',
    body: JSON.stringify(body),
  });
}

/**
 * GET /v1/index — list recent indexing requests.
 * @returns {Promise<Array<{ id: string, player: string, platform: string, startMonth: string, endMonth: string, status: string, gamesIndexed: number, errorMessage: string|null }>>}
 */
export async function listIndexRequests() {
  return request('/v1/index');
}

/**
 * GET /v1/index/{id} — get status of an indexing request.
 * @param {string} id — UUID
 * @returns {Promise<{ id: string, status: string, gamesIndexed: number, errorMessage: string|null }>}
 */
export async function getIndexStatus(id) {
  return request(`/v1/index/${id}`);
}

/**
 * POST /v1/query — run ChessQL query.
 * @param {{ query: string, limit: number, offset: number }} body
 * @returns {Promise<{ games: import('./components/table.js').GameRow[], count: number }>}
 */
export async function query(body) {
  return request('/v1/query', {
    method: 'POST',
    body: JSON.stringify(body),
  });
}
