# Options to speed up initial page load (one_d4 + 1d4_web)

## Current behavior

- **GamesView**: Fetches 500 games in one call (`limit: 500`, `offset: 0`), then client-side paginates (e.g. 25 per page). Each game includes full **PGN** and all **motif_occurrences**.
- **QueryView**: Same shape; user picks limit (default 50) but still full rows + occurrences + PGN.
- **Backend**: `POST /v1/query` always (1) runs the game query, (2) loads **all** motif_occurrences for those game URLs, (3) returns `GameFeatureRow[]` with `pgn` and `occurrences` set for every row.

So the first load is slow due to:

1. **DB**: One big game_features query (with PGN) + one big motif_occurrences query for N games.
2. **Payload**: N × (metadata + full PGN + occurrences map) in a single JSON response.

---

## Option A: Fetch fewer games on initial load

**Idea:** Reduce the initial request size so the first paint is faster.

- **GamesView**: Stop hardcoding 500; fetch a smaller page (e.g. 25 or 50) that matches the first table page. Either:
  - **A1** — Use server-side pagination: initial request `limit: 25, offset: 0`; “Next” calls `offset: 25`, etc. No more “load 500 then slice in memory.”
  - **A2** — Keep client-side pagination but with a smaller cap (e.g. fetch 100 or 250 instead of 500).
- **QueryView**: Already uses a chosen limit; consider lowering default (e.g. 25) so the first run is lighter.

**Pros:** Simple; no API contract change.  
**Cons:** If you still hydrate PGN + occurrences for every row in that page, the response is still heavy for 25–50 games. Best combined with B or C.

---

## Option B: Defer motif_occurrences until row expand

**Idea:** Initial response returns only game metadata (and optionally no PGN). When the user expands a row, fetch that one game’s PGN + motif_occurrences.

**Backend:**

- Add a way to return “list” rows without occurrences (and optionally without PGN):
  - **B1** — New query parameter, e.g. `includeOccurrences=false` (and optionally `includePgn=false`). When `false`, skip `queryOccurrences(gameUrls)` and/or omit PGN from `GameFeatureRow`.
  - **B2** — New endpoint, e.g. `GET /v1/games?query=...&limit=...&offset=...` that returns only list fields (no PGN, no occurrences).
- Add a “game detail” endpoint, e.g. `GET /v1/games/{gameUrl}` or `POST /v1/games/detail` with body `{ "gameUrl": "..." }`, returning a single game with PGN + occurrences.

**Frontend:**

- **GamesView / QueryView**: Initial query with `includeOccurrences=false` (and `includePgn=false` if supported). On row expand, if the row doesn’t have `occurrences` (and PGN), call the game-detail endpoint and merge into state (or use a separate query key per `gameUrl`).
- **GameDetailPanel**: Receives either (a) a game that already has `pgn` and `occurrences`, or (b) a game that triggers a fetch when opened; panel shows loading until the detail request completes.

**Pros:** First load is much smaller (metadata only, or metadata + PGN without occurrences). Expand stays snappy with one small request per opened game.  
**Cons:** Requires API changes and a second request when opening a game (and possibly storing/refilling PGN only on demand).

---

## Option C: Omit PGN from list response; fetch on expand

**Idea:** List endpoint never returns PGN. PGN (and optionally occurrences) are loaded only when the user opens a game.

- **Backend:** Same as B: list response without PGN (and optionally without occurrences). Detail endpoint returns one game with PGN + occurrences.
- **Frontend:** Same as B: list loads fast; on expand, fetch detail and then show board + motifs.

**Pros:** PGN is often the largest part of the payload; dropping it from the list cuts size and DB read cost significantly.  
**Cons:** Same as B (API + second request on expand). Often implemented together with B (list = metadata only; detail = PGN + occurrences).

---

## Option D: Server-side pagination only (no “fetch 500 then slice”)

**Idea:** Never request 500 rows at once. Every page is a new request with `limit` and `offset`.

- **Backend:** Already supports `limit` and `offset`; no change required.
- **GamesView**: Remove the fixed 500 fetch. Use `limit = pageSize` and `offset = page * pageSize`; query key includes `offset` so each page is a separate request. Total count can come from the first response’s `count` if the API returns it, or from a separate count endpoint if you add one.

**Pros:** First load only requests one page (e.g. 25 rows).  
**Cons:** If each row still has PGN + occurrences, 25 rows can still be heavy. Combines well with A (small page) + B/C (no PGN/occurrences on list).

---

## Option E: Two-phase list API (list IDs, then bulk detail)

**Idea:** First request returns only identifiers (e.g. `gameUrl`s) and maybe minimal metadata; second request (or many small ones) fetches full rows for the visible page.

- **Backend:** e.g. `POST /v1/query/list` returns `{ gameUrls: string[], count }` (no PGN, no occurrences). Optional: `POST /v1/games/bulk` with body `{ gameUrls: string[], includePgn?: boolean, includeOccurrences?: boolean }` returns full rows for those URLs.
- **Frontend:** Initial load calls list, shows table with placeholders or minimal fields; then calls bulk for the first page of `gameUrl`s to fill in metadata (and on expand, fetch one game’s PGN + occurrences via detail endpoint).

**Pros:** Very small first response; you only pay for full data for what’s on screen (and on expand).  
**Cons:** More API surface and frontend logic; two round-trips before the first page is “full.”

---

## Recommended combinations

1. **Quick win:** **A + D** — Use server-side pagination and a smaller first page (e.g. 25). No API change. If the response for 25 rows is still slow, add B/C.
2. **Largest gain:** **B + C + D** — List endpoint returns only metadata (no PGN, no occurrences), with pagination; detail endpoint returns one game with PGN + occurrences on row expand. First load is fast; expand is one small request.
3. **Middle ground:** **B + D** — List returns metadata + PGN but no occurrences; detail returns occurrences for one game on expand. First load smaller than today; expand still needs one call for motifs (and you can show PGN/board immediately if you already have PGN from the list).

---

## Summary

| Option | API change | First load | Expand |
|--------|------------|------------|--------|
| A – Fewer games | No | Lighter (fewer rows) | Same (data already there for that page) |
| B – Defer occurrences | Yes (param or list/detail) | Lighter (no occurrences) | One detail request |
| C – Defer PGN | Yes (list/detail) | Much lighter (no PGN) | One detail request |
| D – Server-side pagination | No | One page only | Same |
| E – Two-phase list | Yes (list + bulk/detail) | Minimal then fill page | Same as B/C |

Implementing **B + C + D** (list = metadata only, paginated; detail = PGN + occurrences on expand) gives the fastest first load and keeps expand snappy with a single small request per opened game.
