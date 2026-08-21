/**
 * Cloudflare Worker — serves the SPA from Workers Assets. Short-link
 * redirects live on i.iili.uk (Caddy → r3dr_v2), not here: keeping them
 * off this Worker means the API's per-client rate limit keys on the real
 * client via the consolidated host, and a compat-date bump cannot hand
 * /r/* to the SPA shell.
 */
export default {
  async fetch(request, env) {
    return env.ASSETS.fetch(request);
  },
};
