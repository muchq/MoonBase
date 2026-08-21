# r3dr Domain

URL shortening services and related applications.

## APIs

- [**r3dr_v2 API**](apis/r3dr_v2): The C++ shortener on smithy-cpp (#1359),
  serving `/r3dr/v2/*` behind `api.muchq.com`. Where new work lands.
- [**r3dr API**](apis/r3dr): The original Go service, still serving
  `r3dr.net` on its own storage until deprecation.

## Apps

- [**r3dr Web v2**](apps/r3dr_web_v2): The new `r3dr.net` frontend (#1359
  chunk 2) — a Cloudflare Worker SPA on the 1d4_web stack. Its `/r/{slug}`
  redirect keeps v1's short-link shape, backed by the v2 API.
- [**r3dr Web**](apps/r3dr_web): The old static frontend, served by the Go
  VM; retires with it at DNS cutover (#1359 chunk 3).
