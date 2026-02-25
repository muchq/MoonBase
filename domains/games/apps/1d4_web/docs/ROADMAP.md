# 1d4 Web — UI Roadmap

**See also:** [Backend Roadmap](../../../../../domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/docs/ROADMAP.md) — API, infrastructure, queue, and motif detector plans.

## Current State (Phase 1 — Delivered)

React 18 + TypeScript SPA built with Vite 6, deployed to Cloudflare Workers at [1d4.net](https://1d4.net).

- **Games view** — Browse indexed games; sortable columns (player, ELO, time class, ECO, result, motifs); motif badges with occurrence count tooltips; click-through to chess.com; pagination.
- **Index view** — Enqueue index requests (username, platform, start/end month); status table with auto-polling while any request is pending or processing (TanStack Query `refetchInterval`).
- **Query view** — ChessQL textarea; syntax help; example-chip shortcuts; results table; limit selector.
- **Foundation** — Tailwind CSS v4; react-router-dom v6; TanStack Query v5; Vitest + React Testing Library (30 tests); CI build/typecheck/test job on every PR.

### Hook Points Already in Place

These stubs exist in the codebase and are ready for Phase 2 wiring:

- `GameTable` accepts `onRowClick?: (game: GameRow) => void` — passes a no-op today.
- `OccurrenceRow.ply?: number` — field defined in `src/types.ts`; populated once the backend adds it.
- `react-chessboard` v4 and `chess.js` v1 are already installed.

---

## Phase 2 — Game Detail Viewer

### Goal

Click any game row → see an interactive chess board that replays the game, with the motif occurrences listed alongside. Click an occurrence → board jumps to that position, with the tactical motif highlighted via arrows and square highlights.

### Backend Prerequisites

Two backend changes are required before the board viewer can function:

**1. Add `ply` to `OccurrenceRow`**

`ply` is the half-move index (0-based) needed to seek `chess.js` to the exact position. Move 12 white = ply 22; move 12 black = ply 23.

- `api/dto/OccurrenceRow.java` — add `int ply` field.
- `store/GameFeatureStore.java` (`queryOccurrences`) — add `ply` to SELECT and map to DTO.
  - Derivation: `ply = (moveNumber - 1) * 2 + (side == 'black' ? 1 : 0)`

**2. Expose `pgn` in `GameFeatureRow`**

The board replayer needs the full PGN string.

- `api/dto/GameFeatureRow.java` — add `String pgn` field.
- `store/GameFeatureStore.java` (`fromStore`) — include `pgn` column in SELECT and map to DTO.
- `api/controller/GameController.java` — the `pgn` column is already stored; no new indexing work needed.

Once both changes are deployed, `OccurrenceRow.ply` and `GameFeatureRow.pgn` are populated and the frontend can be wired up.

### Frontend: `GameDetailPanel`

New component displayed below (or as a drawer over) the games table when a row is clicked.

**Component tree:**

```
GameDetailPanel
├── BoardPane
│   ├── Chessboard (react-chessboard)
│   └── MoveControls  (← Prev  ▶ Play  Next →)
└── OccurrenceList
    └── OccurrenceItem (click to jump)
```

**`src/components/GameDetailPanel.tsx`**

```tsx
interface Props {
  game: GameRow;
  onClose: () => void;
}
```

- Receives `GameRow` (which now includes `pgn`).
- Fetches occurrences via `GET /v1/occurrences?gameId=<id>` (or inlined in `GameRow` — see API note).
- Passes current position FEN and motif arrows/highlights to `<Chessboard>`.

**Board position management:**

```ts
// chess.js API
const chess = new Chess();
chess.loadPgn(game.pgn);
const history = chess.history({ verbose: true }); // all moves

// seek to ply N
function seekToPly(ply: number) {
  chess.reset();
  for (let i = 0; i < ply; i++) chess.move(history[i]);
  setFen(chess.fen());
  setCurrentPly(ply);
}
```

**Motif annotations:**

Each motif type maps to a characteristic arrow color rendered by `react-chessboard`'s `customArrows` prop:

| Motif type       | Arrow color  | Notes                               |
|-----------------|--------------|-------------------------------------|
| `fork`           | `#f6c90e`    | Yellow — attacker → each victim     |
| `pin`            | `#e84393`    | Pink — pinned piece → king/anchor   |
| `skewer`         | `#e84393`    | Pink — same as pin                  |
| `discovered_*`   | `#66b2ff`    | Blue — moving piece + revealed ray  |
| `check`          | `#ff4444`    | Red — attacker → king               |
| `promotion`      | `#44cc44`    | Green — promotion square            |

Arrows are built from `OccurrenceRow` fields (`moveNumber`, `side`, `description`). Phase 2 can use heuristic square extraction from `description`; richer data (source/destination squares per piece) can be added to `OccurrenceRow` in a follow-on.

**Wiring `onRowClick`:**

In `GamesView.tsx`, replace the no-op:

```tsx
<GameTable
  games={games}
  onRowClick={(game) => setSelectedGame(game)}
/>
{selectedGame && (
  <GameDetailPanel
    game={selectedGame}
    onClose={() => setSelectedGame(null)}
  />
)}
```

Same pattern in `QueryView.tsx`.

### API Note: Occurrence Data

Two options for delivering occurrence data to the panel:

**Option A — Inline in `GameRow`** (current approach): `GameRow.occurrences` already returns a `Record<motifName, OccurrenceRow[]>`. Add `ply` to `OccurrenceRow`; no new endpoint needed. Simple, but results in occurrence data being fetched on every `GET /v1/games` page load.

**Option B — Separate endpoint**: `GET /v1/games/{gameId}/occurrences` returns occurrences only when needed. Cleaner separation, less bandwidth on the games list. Requires a new controller method and store query.

Recommendation: Start with Option A (minimal backend changes). Migrate to Option B if games list performance degrades due to large occurrence payloads.

### Tests

```
src/__tests__/GameDetailPanel.test.tsx   Board renders at move 1; next/prev controls advance ply;
                                         occurrence click seeks to correct ply; close button works
src/__tests__/seekToPly.test.ts          Unit test for chess.js seek utility
```

### Estimated Scope

- 2 backend files modified (~50 lines)
- 3-4 new frontend files (GameDetailPanel, BoardPane, OccurrenceList, seek utility)
- 2 test files
- ~300 lines of frontend code

---

## Phase 3 — Live Index Stats

### Goal

While an indexing request is processing, show a live progress bar and per-month breakdown rather than just a status badge. No page reload required.

### Backend Prerequisite

The polling endpoint (`GET /v1/index`) already returns `gamesIndexed` and `status`. A richer progress response would include:

- `totalMonths: number` — derived from `startMonth`→`endMonth` range.
- `monthsComplete: number` — how many months have been fully processed.
- Current month being indexed (optional, nice-to-have).

This requires a small addition to `IndexRequest` DTO and store query — or can be computed client-side from `startMonth`/`endMonth`/`gamesIndexed` as a heuristic.

### Frontend Changes

**`IndexView.tsx`** — replace status badge with:

```tsx
<ProgressBar value={monthsComplete} max={totalMonths} />
<span>{gamesIndexed} games indexed so far</span>
```

**`ProgressBar.tsx`** — new shared component, also usable for query result progress.

TanStack Query polling is already wired; only the rendering changes.

### Estimated Scope

- 1 backend DTO change (optional, for accurate progress)
- 1 new component (`ProgressBar`)
- ~50 lines changed in `IndexView.tsx`

---

## Phase 4 — Mobile Polish

### Goal

All three views (Games, Index, Query) are fully usable on a 375px viewport without horizontal scroll or overlapping elements.

### Current Issues (as of Phase 1)

- Games table: 10+ columns overflow on small screens.
- Index form: date inputs are tight on narrow screens.
- Query view: results table has same overflow issue as games table.
- `GameDetailPanel` (Phase 2): board + occurrence list need responsive stacking.

### Approach

**Games / Query tables on mobile:**

Show a card layout below `sm:` breakpoint instead of a table:

```tsx
// Wide screens: <table>
// Narrow screens: stacked cards
<div className="hidden sm:block">
  <GameTable games={games} ... />
</div>
<div className="sm:hidden">
  {games.map(g => <GameCard key={g.gameUrl} game={g} ... />)}
</div>
```

`GameCard.tsx` — compact card showing white/black player, result, motif badges, and played date. Tappable to open `GameDetailPanel`.

**`GameDetailPanel` on mobile:**

- Full-screen drawer (slides up from bottom) on narrow viewports.
- Board fills viewport width; occurrence list scrolls below.
- Tailwind `@media (max-width: sm)` responsive variants throughout.

**Index form:**

- Month inputs stack vertically on mobile.
- Submit button full-width on mobile.

### Estimated Scope

- 1 new component (`GameCard`)
- ~100 lines of responsive Tailwind changes across existing components
- Tests: add `screen.width` mock in jsdom for responsive rendering tests

---

## Phase 5 — Advanced Query UX

### Goal

Make ChessQL easier to write and interpret, especially for users unfamiliar with the query language.

### Features

**Query builder UI:**

Visual form that constructs ChessQL without typing syntax:

```
Player: [_prior     ▾]  Result: [win     ▾]  Motif: [fork    ▾]  Time: [blitz  ▾]
AND  [+ Add filter]                                             [ Run Query ]
```

Emits: `player_white = "_prior" AND result = "win" AND motif(fork) AND time_class = "blitz"`

**Saved queries:**

Store recent queries in `localStorage`; show a "Recent" dropdown. No backend required.

**Result count estimate:**

Show row count from `QueryResponse.count` more prominently; add a "Show all N results" link when results are capped by the limit.

**Motif filter on games view:**

Add a motif multi-select to the Games view that appends `AND motif(X)` to the internal query sent to `GET /v1/games`. Lets users filter the games list by tactic without switching to the Query tab.

### Estimated Scope

- 2-3 new components (QueryBuilder, FilterBar, SavedQueries)
- ~200 lines of new code
- No backend changes required

---

## Phase Dependencies

```
Phase 2 (game detail viewer)   ← needs backend: ply + pgn fields
Phase 3 (live index stats)     ← optional backend enhancement; UI change is self-contained
Phase 4 (mobile polish)        ← independent; can start any time; builds on Phase 2 layout
Phase 5 (advanced query UX)    ← independent; no backend changes
```

Phase 2 is the highest-value next step and the only one gated on backend changes.
Phases 3–5 are independent of each other and can be done in any order.
