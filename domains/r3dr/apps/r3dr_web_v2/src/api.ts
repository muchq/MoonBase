// The v2 API (domains/r3dr/apis/r3dr_v2), served at api.muchq.com behind
// CORS for this origin. VITE_API_BASE overrides for local experiments.
const API_BASE: string =
  (import.meta.env.VITE_API_BASE as string | undefined) ?? 'https://api.muchq.com';

// Short links keep v1's shape: the worker 302s /r/{slug} to the API.
// VITE_SHORT_LINK_BASE points minted links at a preview host before the DNS
// flip — until then, r3dr.net still resolves through the old stack.
const SHORT_LINK_BASE: string =
  (import.meta.env.VITE_SHORT_LINK_BASE as string | undefined) ?? 'https://r3dr.net/r/';

export function shortLink(slug: string): string {
  return `${SHORT_LINK_BASE}${slug}`;
}

// Two error shapes: generated trait validation ({fieldList: [{message}...],
// message}) and modeled errors ({message}). The fieldList entry names the
// one broken constraint; the top-level message prefixes a count on it.
function errorMessage(body: string | null): string | null {
  if (!body) return null;
  try {
    const parsed: unknown = JSON.parse(body);
    if (parsed !== null && typeof parsed === 'object') {
      const fieldList = (parsed as { fieldList?: { message?: unknown }[] }).fieldList;
      if (Array.isArray(fieldList) && typeof fieldList[0]?.message === 'string') {
        return fieldList[0].message;
      }
      const message = (parsed as { message?: unknown }).message;
      if (typeof message === 'string') {
        return message;
      }
    }
  } catch (_) {
    // not JSON
  }
  return body;
}

export async function shorten(longUrl: string, expiresAt: number): Promise<{ slug: string }> {
  let res: Response;
  try {
    res = await fetch(`${API_BASE}/r3dr/v1/shorten`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ longUrl, expiresAt }),
      // A dead network must hand the form back, not hold it hostage.
      signal: AbortSignal.timeout(10_000),
    });
  } catch (_) {
    throw new Error('Network trouble — check your connection and try again.');
  }
  if (!res.ok) {
    let body: string | null = null;
    try {
      body = await res.text();
    } catch (_) {
      // ignore
    }
    const message =
      res.status === 429
        ? 'Slow down — too many links. Try again in a minute.'
        : errorMessage(body) || res.statusText || 'Something went wrong';
    throw new Error(message);
  }
  return res.json();
}
