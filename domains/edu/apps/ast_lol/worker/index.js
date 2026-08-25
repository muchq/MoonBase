/**
 * Cloudflare Worker — serves the SPA's static assets. The app is fully
 * client-side (the grader runs in a browser Web Worker); there is no API.
 */
export default {
  async fetch(request, env) {
    return env.ASSETS.fetch(request);
  },
};
