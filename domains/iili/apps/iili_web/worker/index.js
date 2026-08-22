/**
 * Cloudflare Worker — serves the SPA from Workers Assets. Short-link
 * redirects live on i.iili.uk (Caddy → iili), not here: keeping them
 * off this Worker means the API's per-client rate limit keys on the real
 * client via the consolidated host, and a compat-date bump cannot hand
 * /r/* to the SPA shell.
 *
 * No compat hop for iili.uk/r/{slug}: nothing was minted on that shape
 * in the #1430 Worker window before minting moved to i.iili.uk.
 */
export default {
  async fetch(request, env) {
    return env.ASSETS.fetch(request);
  },
};
