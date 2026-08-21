# r3dr Domain

URL shortening services and related applications.

## APIs

- [**r3dr_v2 API**](apis/r3dr_v2): The C++ shortener on smithy-cpp (#1359),
  serving `/r3dr/v2/*` behind `api.muchq.com`. Where new work lands.
- [**r3dr API**](apis/r3dr): The original Go service, still serving
  `r3dr.net` on its own storage until deprecation.

## Apps

- [**iili**](apps/iili_web): The standalone frontend (#1359 chunk 2) — a
  Cloudflare Worker SPA on the 1d4_web stack at `iili.uk`, whose
  `/r/{slug}` redirect fronts the v2 API. A second frontend lives at
  muchq.com/r3dr (the muchq.github.io repo).
- [**r3dr Web**](apps/r3dr_web): The old static frontend, served with the
  Go service; retires with it (#1359 chunk 3).
