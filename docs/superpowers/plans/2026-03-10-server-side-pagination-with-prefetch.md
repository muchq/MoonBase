# Server-Side Pagination with Prefetch Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace GamesView's single 500-game fetch with server-side pagination (25 games/page) and TanStack Query prefetching of the next page, so both initial load and page-to-page navigation are fast.

**Architecture:** The backend already supports `limit`/`offset` but lacks a stable `ORDER BY`, which makes OFFSET pagination non-deterministic. We add `ORDER BY played_at DESC, game_url ASC` to the DAO. The frontend switches to page-keyed queries and uses `queryClient.prefetchQuery` to pre-populate the next page in the cache while the user reads the current one. A minimal `hasMore` prop addition to `Pagination` avoids the need for an expensive `COUNT(*)` backend query.

**Tech Stack:** Java/JDBI3 (backend), React 18 + TanStack Query v5 + TypeScript (frontend), Vitest + RTL (frontend tests), JUnit 4 + H2 (backend tests)

---

## Chunk 1: Backend — Stable ORDER BY

### Task 1: Add stable ORDER BY to GameFeatureDao.query()

**Files:**
- Modify: `domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/GameFeatureDao.java:188`
- Test: `domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/db/GameFeatureDaoTest.java`

**Context:** `GameFeatureDao.query()` currently builds SQL as `{compiledSql} LIMIT ? OFFSET ?` with no ORDER BY. PostgreSQL is free to return rows in any order across pages — duplicates and skips are possible. We need `ORDER BY played_at DESC, game_url ASC` for stable keyset-free pagination. `game_url` is a unique column, so it breaks ties deterministically.

- [ ] **Step 1: Write a failing test for stable pagination order**

Add to `GameFeatureDaoTest.java` after the existing `insertBatch_insertsMultipleGames` test:

```java
@Test
public void query_returnsGamesInStableDescendingPlayedAtOrder() {
  // Insert games with different played_at values
  Instant older = Instant.parse("2024-01-01T00:00:00Z");
  Instant newer = Instant.parse("2024-06-01T00:00:00Z");
  dao.insertBatch(List.of(
      createGameAt("https://chess.com/game/order-a", older),
      createGameAt("https://chess.com/game/order-b", newer)
  ));

  CompiledQuery allGames = new SqlCompiler().compile(Parser.parse("white_elo >= 1000"));
  List<GameFeature> page1 = dao.query(allGames, 1, 0);
  List<GameFeature> page2 = dao.query(allGames, 1, 1);

  assertThat(page1).hasSize(1);
  assertThat(page2).hasSize(1);
  // Newer game comes first (DESC), older game is on page 2
  assertThat(page1.get(0).gameUrl()).isEqualTo("https://chess.com/game/order-b");
  assertThat(page2.get(0).gameUrl()).isEqualTo("https://chess.com/game/order-a");
}
```

Also add the `createGameAt` helper to `GameFeatureDaoTest` alongside the existing `createGame` helper:

```java
private GameFeature createGameAt(String url, Instant playedAt) {
  return new GameFeature(
      UUID.randomUUID(), requestId, url,
      "chess.com", "white", "black",
      1500, 1480, "blitz", "A00", "1-0",
      playedAt, 30, Instant.now(), "1. e4 e5 *");
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/andy/src/MoonBase
bazel test //domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/db:GameFeatureDaoTest --test_output=short 2>&1 | grep -E "FAIL|PASS|ERROR|query_returns"
```

Expected: FAIL (no ORDER BY means order is unpredictable)

- [ ] **Step 3: Add ORDER BY to GameFeatureDao.query()**

In `GameFeatureDao.java`, change line 188:

```java
// Before:
String sql = cq.selectSql() + " LIMIT ? OFFSET ?";

// After:
String sql = cq.selectSql() + " ORDER BY played_at DESC, game_url ASC LIMIT ? OFFSET ?";
```

- [ ] **Step 4: Run test to verify it passes**

```bash
bazel test //domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/db:GameFeatureDaoTest --test_output=short
```

Expected: all tests PASS

- [ ] **Step 5: Run the full backend test suite**

```bash
bazel test //domains/games/apis/one_d4/... --test_output=short
```

Expected: all tests PASS

- [ ] **Step 6: Commit**

```bash
cd /Users/andy/src/MoonBase
git checkout -b perf/server-side-pagination
git add domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/GameFeatureDao.java \
        domains/games/apis/one_d4/src/test/java/com/muchq/games/one_d4/db/GameFeatureDaoTest.java
git commit -m "fix(one_d4): add stable ORDER BY to game query for correct OFFSET pagination"
```

---

## Chunk 2: Frontend — Pagination component hasMore prop

### Task 2: Add optional hasMore prop to Pagination

**Files:**
- Modify: `domains/games/apps/1d4_web/src/components/Pagination.tsx`

**Context:** The current `Pagination` component computes `disabled={offset + limit >= total}` for the Next button and shows "X–Y of Z" in the label. With server-side pagination, we don't have the total count — only whether the current page was full (which implies more pages exist). We add an optional `hasMore?: boolean` prop: when provided it overrides the Next button's disabled state. Everything else (Prev button, display, page size selector) is unchanged.

- [ ] **Step 1: Write a failing test for the hasMore prop**

In a new file `domains/games/apps/1d4_web/src/__tests__/Pagination.test.tsx`:

```typescript
import React from 'react';
import { render, screen } from '@testing-library/react';
import { describe, it, expect } from 'vitest';
import Pagination from '../components/Pagination';

describe('Pagination', () => {
  it('enables Next when hasMore=true even if offset+limit >= total', () => {
    render(
      <Pagination
        offset={0}
        limit={25}
        total={25}
        onLimitChange={() => {}}
        onPrev={() => {}}
        onNext={() => {}}
        hasMore={true}
      />
    );
    expect(screen.getByRole('button', { name: 'Next' })).not.toBeDisabled();
  });

  it('disables Next when hasMore=false', () => {
    render(
      <Pagination
        offset={0}
        limit={25}
        total={100}
        onLimitChange={() => {}}
        onPrev={() => {}}
        onNext={() => {}}
        hasMore={false}
      />
    );
    expect(screen.getByRole('button', { name: 'Next' })).toBeDisabled();
  });

  it('falls back to offset+limit>=total when hasMore is not provided', () => {
    render(
      <Pagination
        offset={75}
        limit={25}
        total={100}
        onLimitChange={() => {}}
        onPrev={() => {}}
        onNext={() => {}}
      />
    );
    expect(screen.getByRole('button', { name: 'Next' })).toBeDisabled();
  });
});
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test -- --reporter=verbose Pagination.test 2>&1 | tail -20
```

Expected: FAIL (Pagination doesn't have `hasMore` prop yet)

- [ ] **Step 3: Add hasMore prop to Pagination.tsx**

Replace the contents of `Pagination.tsx` with:

```typescript
const DEFAULT_PAGE_SIZES = [10, 25, 50, 100];

interface Props {
  offset: number;
  limit: number;
  total: number;
  onLimitChange: (limit: number) => void;
  onPrev: () => void;
  onNext: () => void;
  pageSizes?: number[];
  hasMore?: boolean;
}

export default function Pagination({
  offset,
  limit,
  total,
  onLimitChange,
  onPrev,
  onNext,
  pageSizes = DEFAULT_PAGE_SIZES,
  hasMore,
}: Props) {
  const start = total === 0 ? 0 : Math.min(offset + 1, total);
  const end = Math.min(offset + limit, total);
  const nextDisabled = hasMore !== undefined ? !hasMore : offset + limit >= total;

  return (
    <div className="pagination">
      <select
        value={limit}
        onChange={(e) => onLimitChange(Number(e.target.value))}
      >
        {pageSizes.map((n) => (
          <option key={n} value={n}>
            {n} per page
          </option>
        ))}
      </select>
      <button className="btn" disabled={offset === 0} onClick={onPrev}>
        Previous
      </button>
      <button
        className="btn"
        disabled={nextDisabled}
        onClick={onNext}
      >
        Next
      </button>
      <span style={{ color: 'var(--text-muted)' }}>
        {total === 0 ? '0 results' : `${start}–${end} of ${total}`}
      </span>
    </div>
  );
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test -- --reporter=verbose Pagination.test 2>&1 | tail -20
```

Expected: all 3 tests PASS

- [ ] **Step 5: Run full frontend test suite to check for regressions**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test 2>&1 | tail -20
```

Expected: all tests PASS

- [ ] **Step 6: Commit**

```bash
cd /Users/andy/src/MoonBase
git add domains/games/apps/1d4_web/src/components/Pagination.tsx \
        domains/games/apps/1d4_web/src/__tests__/Pagination.test.tsx
git commit -m "feat(1d4_web): add optional hasMore prop to Pagination for server-side pagination support"
```

---

## Chunk 3: Frontend — GamesView server-side pagination with prefetch

### Task 3: Convert GamesView to server-side pagination with prefetch

**Files:**
- Modify: `domains/games/apps/1d4_web/src/views/GamesView.tsx`
- Modify: `domains/games/apps/1d4_web/src/__tests__/GamesView.test.tsx`

**Context:** Currently GamesView fetches all 500 games in one request and paginates in memory. We change it to:
1. Fetch `pageSize` games at the server's `ORDER BY played_at DESC` order (no `limit: 500`)
2. After data loads, fire `prefetchQuery` for the next page — it runs in the background
3. Use `hasMore = games.length === pageSize` to know if Next should be enabled (no COUNT query needed)
4. Keep `sortGames()` — it now sorts only the 25 games on the current page, which is instant
5. Reset to page 0 when username changes

The query key is `['games', queryText, page, pageSize]` — page changes trigger new fetches, and prefetched pages are already in cache.

- [ ] **Step 1: Write the updated tests first**

Replace `GamesView.test.tsx`:

```typescript
import React from 'react';
import { render, screen, waitFor, fireEvent, act } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router-dom';
import GamesView from '../views/GamesView';
import * as api from '../api';
import type { GameRow } from '../types';

vi.mock('../api');
vi.mock('react-chessboard', () => ({
  Chessboard: ({ position }: { position: string }) => (
    <div data-testid="chessboard" data-fen={position} />
  ),
}));

const mockGame: GameRow = {
  gameUrl: 'https://chess.com/game/99',
  platform: 'chess.com',
  whiteUsername: '_prior',
  blackUsername: 'OpponentA',
  whiteElo: 1500,
  blackElo: 1480,
  timeClass: 'blitz',
  eco: 'A00',
  result: '1-0',
  playedAt: 1700000000,
  indexedAt: 1700001000,
  numMoves: 30,
  occurrences: { fork: [{ gameUrl: 'https://chess.com/game/99', motif: 'fork', moveNumber: 10, side: 'white', description: 'Fork' }] },
};

function makeWrapper() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return function Wrapper({ children }: { children: React.ReactNode }) {
    return (
      <QueryClientProvider client={qc}>
        <MemoryRouter>{children}</MemoryRouter>
      </QueryClientProvider>
    );
  };
}

describe('GamesView', () => {
  beforeEach(() => {
    vi.mocked(api.query).mockResolvedValue({ games: [mockGame], count: 1 });
  });

  it('shows loading state then renders game rows', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    expect(screen.getByText('Loading…')).toBeInTheDocument();
    await waitFor(() => expect(screen.getByText('_prior')).toBeInTheDocument());
  });

  it('renders motif badges for games with motifs', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));
    expect(screen.getByText('fork')).toBeInTheDocument();
  });

  it('uses username filter when Search is clicked', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    const input = screen.getByPlaceholderText('e.g. Hikaru');
    fireEvent.change(input, { target: { value: '_prior' } });
    fireEvent.click(screen.getByRole('button', { name: 'Search' }));

    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({
          query: expect.stringContaining('_prior'),
        })
      )
    );
  });

  it('fetches with correct limit and offset on initial load', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    // Initial load: page 0, pageSize 25 → offset 0, limit 25
    expect(api.query).toHaveBeenCalledWith(
      expect.objectContaining({ limit: 25, offset: 0 })
    );
  });

  it('opens game detail panel when a row is clicked', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));
    const rows = screen.getAllByRole('row');
    fireEvent.click(rows[1]);
    expect(screen.getByText('_prior vs OpponentA')).toBeInTheDocument();
  });

  it('submits search on Enter key in the username input', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    const input = screen.getByPlaceholderText('e.g. Hikaru');
    fireEvent.change(input, { target: { value: 'Hikaru' } });
    fireEvent.keyDown(input, { key: 'Enter' });

    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({ query: expect.stringContaining('Hikaru') })
      )
    );
  });

  it('prefetches next page after initial load', async () => {
    // Return full page (25 games) to trigger hasMore=true and prefetch
    const fullPage = Array.from({ length: 25 }, (_, i) => ({
      ...mockGame,
      gameUrl: `https://chess.com/game/${i}`,
    }));
    vi.mocked(api.query).mockResolvedValue({ games: fullPage, count: 25 });

    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getAllByRole('row').length > 1);

    // Prefetch fires a second query call for offset=25
    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({ limit: 25, offset: 25 })
      )
    );
  });
});
```

- [ ] **Step 2: Run the updated tests to see which ones fail**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test -- --reporter=verbose GamesView.test 2>&1 | tail -30
```

Expected: `fetches with correct limit and offset` and `prefetches next page` tests FAIL (still using `limit: 500`)

- [ ] **Step 3: Implement server-side pagination with prefetch in GamesView.tsx**

Replace the contents of `GamesView.tsx` with:

```typescript
import { useState, useMemo, useEffect } from 'react';
import { useQuery, useQueryClient } from '@tanstack/react-query';
import { query as apiQuery } from '../api';
import type { GameRow } from '../types';
import GameTable from '../components/GameTable';
import Pagination from '../components/Pagination';

const DEFAULT_PAGE_SIZE = 25;
const DEFAULT_QUERY = 'num.moves >= 0';

function escapeChessQLString(s: string): string {
  return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function buildQuery(username: string): string {
  const u = username.trim();
  if (!u) return DEFAULT_QUERY;
  const escaped = escapeChessQLString(u);
  return `white.username = "${escaped}" OR black.username = "${escaped}"`;
}

function sortGames(
  list: GameRow[],
  sortBy: string,
  sortDir: 'asc' | 'desc'
): GameRow[] {
  if (!sortBy || sortBy === 'motifs') return list;
  return [...list].sort((a, b) => {
    const ga = a as unknown as Record<string, unknown>;
    const gb = b as unknown as Record<string, unknown>;
    let va = ga[sortBy];
    let vb = gb[sortBy];
    if (sortBy === 'playedAt') {
      va =
        typeof va === 'number'
          ? va
          : va
            ? new Date(va as string).getTime() / 1000
            : 0;
      vb =
        typeof vb === 'number'
          ? vb
          : vb
            ? new Date(vb as string).getTime() / 1000
            : 0;
    }
    if (va == null) return sortDir === 'asc' ? -1 : 1;
    if (vb == null) return sortDir === 'asc' ? 1 : -1;
    const cmp = va < vb ? -1 : va > vb ? 1 : 0;
    return sortDir === 'asc' ? cmp : -cmp;
  });
}

export default function GamesView() {
  const [usernameInput, setUsernameInput] = useState('');
  const [username, setUsername] = useState('');
  const [sortBy, setSortBy] = useState('playedAt');
  const [sortDir, setSortDir] = useState<'asc' | 'desc'>('desc');
  const [page, setPage] = useState(0);
  const [pageSize, setPageSize] = useState(DEFAULT_PAGE_SIZE);
  const [selectedGame, setSelectedGame] = useState<GameRow | null>(null);

  const queryClient = useQueryClient();
  const queryText = buildQuery(username);
  const offset = page * pageSize;

  const { data, isLoading, error } = useQuery({
    queryKey: ['games', queryText, page, pageSize],
    queryFn: () => apiQuery({ query: queryText, limit: pageSize, offset }),
  });

  const games = data?.games ?? [];
  const hasMore = games.length === pageSize;

  // Prefetch the next page while the user is reading the current one
  useEffect(() => {
    if (!hasMore) return;
    queryClient.prefetchQuery({
      queryKey: ['games', queryText, page + 1, pageSize],
      queryFn: () =>
        apiQuery({ query: queryText, limit: pageSize, offset: offset + pageSize }),
    });
  }, [queryClient, queryText, page, pageSize, offset, hasMore]);

  const sorted = useMemo(
    () => sortGames(games, sortBy, sortDir),
    [games, sortBy, sortDir]
  );

  function handleSort(col: string) {
    if (sortBy === col) {
      setSortDir((d) => (d === 'asc' ? 'desc' : 'asc'));
    } else {
      setSortBy(col);
      setSortDir(col === 'playedAt' ? 'desc' : 'asc');
    }
  }

  function handleSearch() {
    setUsername(usernameInput);
    setPage(0);
  }

  return (
    <>
      <div className="panel">
        <h2>Indexed games</h2>
        <p className="text-muted">
          Browse indexed games. Optionally filter by username (exact match,
          white or black).
        </p>
        <div
          className="form-group"
          style={{
            marginTop: '0.75rem',
            display: 'flex',
            flexWrap: 'wrap',
            gap: '0.5rem',
            alignItems: 'center',
          }}
        >
          <label htmlFor="games-username" style={{ marginBottom: 0 }}>
            Username
          </label>
          <input
            id="games-username"
            type="text"
            placeholder="e.g. Hikaru"
            value={usernameInput}
            style={{ maxWidth: '200px' }}
            onChange={(e) => setUsernameInput(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') {
                e.preventDefault();
                handleSearch();
              }
            }}
          />
          <button type="button" className="btn" onClick={handleSearch}>
            Search
          </button>
        </div>
      </div>

      {error && (
        <div className="message error">{(error as Error).message}</div>
      )}
      {isLoading && <div className="loading">Loading…</div>}

      {!isLoading && !error && (
        <>
          <GameTable
            games={sorted}
            sortBy={sortBy}
            sortDir={sortDir}
            onSort={handleSort}
            onRowClick={(game) =>
              setSelectedGame((prev) =>
                prev?.gameUrl === game.gameUrl ? null : game
              )
            }
            selectedGame={selectedGame}
            onClose={() => setSelectedGame(null)}
          />
          <Pagination
            offset={offset}
            limit={pageSize}
            total={offset + sorted.length}
            hasMore={hasMore}
            onLimitChange={(n) => {
              setPageSize(n);
              setPage(0);
            }}
            onPrev={() => setPage((p) => Math.max(0, p - 1))}
            onNext={() => setPage((p) => p + 1)}
          />
        </>
      )}
    </>
  );
}
```

- [ ] **Step 4: Run the tests to verify they all pass**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test -- --reporter=verbose GamesView.test 2>&1 | tail -30
```

Expected: all tests PASS

- [ ] **Step 5: Run typecheck**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm run typecheck 2>&1 | tail -20
```

Expected: no errors

- [ ] **Step 6: Run full frontend test suite**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test 2>&1 | tail -20
```

Expected: all tests PASS

- [ ] **Step 7: Commit**

```bash
cd /Users/andy/src/MoonBase
git add domains/games/apps/1d4_web/src/views/GamesView.tsx \
        domains/games/apps/1d4_web/src/__tests__/GamesView.test.tsx
git commit -m "perf(1d4_web): server-side pagination with next-page prefetch in GamesView"
```

---

## Final Verification

- [ ] **Run all backend tests**

```bash
cd /Users/andy/src/MoonBase
bazel test //domains/games/... --test_output=short
```

- [ ] **Run all frontend tests and typecheck**

```bash
cd /Users/andy/src/MoonBase/domains/games/apps/1d4_web
npm test && npm run typecheck
```

- [ ] **Manual smoke test** — start the dev server and verify:
  - Initial load of GamesView is fast (25 games)
  - Clicking Next page is instant (prefetched)
  - Motif badges appear in the table
  - Sorting works on the current page
  - Username search resets to page 0

---

## What Changed and What Didn't

**Changed:**
- `GameFeatureDao.query()`: adds `ORDER BY played_at DESC, game_url ASC` for stable pagination
- `Pagination.tsx`: adds optional `hasMore?: boolean` prop (backwards compatible — existing callers work unchanged)
- `GamesView.tsx`: fetches 25 games per page instead of 500; prefetches next page; `page` state replaces `offset` state; `sortGames()` now sorts the current page only

**Unchanged:**
- Backend API shape (`QueryRequest`, `QueryResponse`, `/v1/query` endpoint)
- Motif badge display in GameTable — occurrences are still included in each page response
- QueryView — not affected by this change
- Sorting — still works, now applies to the 25 games on the current page
- GameDetailPanel — unchanged, still receives the full `GameRow` with occurrences on click
