# r3dr Domain

URL shortening services and related applications.

## APIs

- [**r3dr_v2 API**](apis/r3dr_v2): The C++ shortener on smithy-cpp (#1359),
  serving `/r3dr/v2/*` behind `api.muchq.com`. The original Go service and
  its `r3dr.net` frontage retired in #1359 chunk 3; v2's slug sequence is
  floored above the space v1 ever minted, so old slugs can't be aliased.

## Apps

- [**iili**](apps/iili_web): The standalone frontend (#1359 chunk 2) — a
  Cloudflare Worker SPA at `iili.uk`. Short links mint as
  `i.iili.uk/r/{slug}` (Caddy on the consolidated host → r3dr_v2). A
  second frontend lives at muchq.com/r3dr (the muchq.github.io repo).
