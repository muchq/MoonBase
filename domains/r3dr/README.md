# r3dr Domain

URL shortening services and related applications.

## APIs

- [**r3dr_v2 API**](apis/r3dr_v2): The C++ shortener on smithy-cpp (#1359),
  serving `/r3dr/v2/*` behind `api.muchq.com`. Where new work lands.
- [**r3dr API**](apis/r3dr): The original Go service — still in compose
  until #1359 chunk 3, but no longer Caddy-fronted (`r3dr.net` sites
  removed; those short links are dead).

## Apps

- [**iili**](apps/iili_web): The standalone frontend (#1359 chunk 2) — a
  Cloudflare Worker SPA at `iili.uk`. Short links mint as
  `i.iili.uk/r/{slug}` (Caddy on the consolidated host → r3dr_v2). A
  second frontend lives at muchq.com/r3dr (the muchq.github.io repo).
- [**r3dr Web**](apps/r3dr_web): The old static frontend; was served from
  `/var/www/r3dr` with the Go service. That mount is gone with the
  r3dr.net Caddy sites; the files retire with the service in chunk 3.
