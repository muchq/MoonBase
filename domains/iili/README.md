# iili Domain

URL shortening services and related applications.

## APIs

- [**iili API**](apis/iili): The C++ shortener on smithy-cpp (#1359),
  serving `/iili/v1/*` behind `api.muchq.com`.

## Apps

- [**iili web**](apps/iili_web): The standalone frontend (#1359) — a
  Cloudflare Worker SPA at `iili.uk`. Short links mint as
  `i.iili.uk/r/{slug}` (Caddy on the consolidated host → iili). A
  second frontend lives at muchq.com/iili (the muchq.github.io repo).
