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
 * POST /index — enqueue an index request.
 * @param {{ player: string, platform: string, startMonth: string, endMonth: string }} body
 * @returns {Promise<{ id: string, status: string, gamesIndexed: number, errorMessage: string|null }>}
 */
export async function createIndex(body) {
  return request('/index', {
    method: 'POST',
    body: JSON.stringify(body),
  });
}

/**
 * GET /index/{id} — get status of an indexing request.
 * @param {string} id — UUID
 * @returns {Promise<{ id: string, status: string, gamesIndexed: number, errorMessage: string|null }>}
 */
export async function getIndexStatus(id) {
  return request(`/index/${id}`);
}

/**
 * POST /query — run ChessQL query.
 * @param {{ query: string, limit: number, offset: number }} body
 * @returns {Promise<{ games: import('./components/table.js').GameRow[], count: number }>}
 */
export async function query(body) {
  return request('/query', {
    method: 'POST',
    body: JSON.stringify(body),
  });
}
