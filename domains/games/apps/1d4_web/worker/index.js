/**
 * Cloudflare Worker — serves static assets from src/.
 * All API calls go to api.1d4.net from the browser; this worker only serves the SPA.
 */
export default {
  async fetch(request, env) {
    return env.ASSETS.fetch(request);
  },
};
