/**
 * Cloudflare Worker — 302s short links to the v2 API and serves the SPA for
 * everything else. 302 not 301: links expire, browsers must not cache the
 * hop. The redirect goes to the API directly so its per-client rate limit
 * keys on the real client, not this worker's egress IPs. HEAD gets the same
 * answer as GET; anything else falls through to the SPA.
 */
const API_BASE = 'https://api.muchq.com';

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const slug = /^\/r\/([^/]+)\/?$/.exec(url.pathname)?.[1];
    if (slug && (request.method === 'GET' || request.method === 'HEAD')) {
      return Response.redirect(`${API_BASE}/r3dr/v1/r/${slug}`, 302);
    }
    return env.ASSETS.fetch(request);
  },
};
