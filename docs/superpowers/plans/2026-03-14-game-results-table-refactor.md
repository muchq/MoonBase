# GameResultsTable Refactor Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract a shared `GameResultsTable` component that owns accordion selection state, and use it in both `GamesView` and `QueryView` so both open the game detail panel inline (accordion style).

**Architecture:** Create `GameResultsTable` wrapping `GameTable` with internal `selectedGame` state (toggle-to-close, auto-clear on `games` change). Make `GameTable`'s sort props optional with defaults so the wrapper doesn't need to pass no-ops. Update both views to use `GameResultsTable`, removing their own selection state and (in `QueryView`) the standalone `GameDetailPanel`.

**Tech Stack:** React 18, TypeScript, Vitest + @testing-library/react

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `src/components/GameTable.tsx` | Make `sortBy`, `sortDir`, `onSort` optional with defaults |
| Create | `src/components/GameResultsTable.tsx` | Wraps `GameTable`; owns `selectedGame` state |
| Create | `src/__tests__/GameResultsTable.test.tsx` | Tests for the new wrapper |
| Modify | `src/views/GamesView.tsx` | Use `GameResultsTable`; drop local `selectedGame` state |
| Modify | `src/views/QueryView.tsx` | Use `GameResultsTable`; drop standalone `GameDetailPanel` |
| (unchanged) | `src/__tests__/GameTable.test.tsx` | Existing tests still pass (props still accepted) |
| (unchanged) | `src/__tests__/GamesView.test.tsx` | Existing tests still pass (behavior unchanged) |
| (unchanged) | `src/__tests__/QueryView.test.tsx` | Existing tests still pass (accordion test at line 99 still works) |

All commands run from `domains/games/apps/1d4_web/`.

---

## Chunk 1: GameTable optional sort props + GameResultsTable component

### Task 1: Make GameTable sort props optional

**Files:**
- Modify: `src/components/GameTable.tsx` (Props interface + destructuring)

- [ ] **Step 1: Make `sortBy`, `sortDir`, `onSort` optional in `GameTable.Props` and add defaults**

  In `src/components/GameTable.tsx`, change the `Props` interface and function signature:

  ```tsx
  interface Props {
    games: GameRow[];
    sortBy?: string;           // was: sortBy: string
    sortDir?: 'asc' | 'desc'; // was: sortDir: 'asc' | 'desc'
    onSort?: (col: string) => void; // was: onSort: (col: string) => void
    onRowClick?: (game: GameRow) => void;
    selectedGame?: GameRow | null;
    onClose?: () => void;
  }

  export default function GameTable({
    games,
    sortBy = '',
    sortDir = 'asc',
    onSort = () => {},
    onRowClick,
    selectedGame,
    onClose,
  }: Props) {
  ```

- [ ] **Step 2: Run existing GameTable tests to confirm they still pass**

  ```bash
  npx vitest run src/__tests__/GameTable.test.tsx
  ```

  Expected: all 8 tests PASS (the tests pass `sortBy`/`sortDir`/`onSort` explicitly, so optional defaults don't affect them).

- [ ] **Step 3: Commit**

  ```bash
  git add src/components/GameTable.tsx
  git commit -m "refactor(1d4_web): make GameTable sort props optional with defaults"
  ```

---

### Task 2: Create GameResultsTable with tests

**Files:**
- Create: `src/__tests__/GameResultsTable.test.tsx`
- Create: `src/components/GameResultsTable.tsx`

- [ ] **Step 1: Write the failing tests**

  Create `src/__tests__/GameResultsTable.test.tsx`:

  ```tsx
  import { render, screen, fireEvent } from '@testing-library/react';
  import { describe, it, expect, vi } from 'vitest';
  import GameResultsTable from '../components/GameResultsTable';
  import type { GameRow } from '../types';

  vi.mock('react-chessboard', () => ({
    Chessboard: ({ position }: { position: string }) => (
      <div data-testid="chessboard" data-fen={position} />
    ),
  }));

  const game1: GameRow = {
    gameUrl: 'https://chess.com/game/1',
    platform: 'chess.com',
    whiteUsername: 'Alice',
    blackUsername: 'Bob',
    whiteElo: 1800,
    blackElo: 1750,
    timeClass: 'blitz',
    eco: 'B90',
    result: '1-0',
    playedAt: 1700000000,
    indexedAt: 1700001000,
    numMoves: 40,
    occurrences: {},  // required for GameDetailPanel to render without crashing
  };

  const game2: GameRow = {
    gameUrl: 'https://chess.com/game/2',
    platform: 'chess.com',
    whiteUsername: 'Carol',
    blackUsername: 'Dave',
    whiteElo: 2100,
    blackElo: 2050,
    timeClass: 'rapid',
    eco: 'C20',
    result: '0-1',
    playedAt: 1700100000,
    indexedAt: 1700101000,
    numMoves: 55,
    occurrences: {},
  };

  describe('GameResultsTable', () => {
    it('renders a row for each game', () => {
      render(<GameResultsTable games={[game1, game2]} />);
      expect(screen.getByText('Alice')).toBeInTheDocument();
      expect(screen.getByText('Carol')).toBeInTheDocument();
    });

    it('shows no detail panel initially', () => {
      render(<GameResultsTable games={[game1]} />);
      expect(screen.queryByText('Alice vs Bob')).not.toBeInTheDocument();
    });

    it('opens detail panel inline when a row is clicked', () => {
      render(<GameResultsTable games={[game1]} />);
      fireEvent.click(screen.getByText('Alice'));
      expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
    });

    it('closes detail panel when the same row is clicked again (toggle)', () => {
      render(<GameResultsTable games={[game1]} />);
      fireEvent.click(screen.getByText('Alice'));
      expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
      fireEvent.click(screen.getByText('Alice'));
      expect(screen.queryByText('Alice vs Bob')).not.toBeInTheDocument();
    });

    it('switches detail panel when a different row is clicked', () => {
      render(<GameResultsTable games={[game1, game2]} />);
      fireEvent.click(screen.getByText('Alice'));
      expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
      fireEvent.click(screen.getByText('Carol'));
      expect(screen.queryByText('Alice vs Bob')).not.toBeInTheDocument();
      expect(screen.getByText('Carol vs Dave')).toBeInTheDocument();
    });

    it('clears detail panel when games prop changes', () => {
      const { rerender } = render(<GameResultsTable games={[game1]} />);
      fireEvent.click(screen.getByText('Alice'));
      expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
      rerender(<GameResultsTable games={[game2]} />);
      expect(screen.queryByText('Alice vs Bob')).not.toBeInTheDocument();
    });
  });
  ```

- [ ] **Step 2: Run tests to confirm they fail (component doesn't exist yet)**

  ```bash
  npx vitest run src/__tests__/GameResultsTable.test.tsx
  ```

  Expected: FAIL — "Cannot find module '../components/GameResultsTable'"

- [ ] **Step 3: Implement GameResultsTable**

  Create `src/components/GameResultsTable.tsx`:

  ```tsx
  import { useState, useEffect } from 'react';
  import type { GameRow } from '../types';
  import GameTable from './GameTable';

  interface Props {
    games: GameRow[];
    sortBy?: string;
    sortDir?: 'asc' | 'desc';
    onSort?: (col: string) => void;
  }

  export default function GameResultsTable({ games, sortBy, sortDir, onSort }: Props) {
    const [selectedGame, setSelectedGame] = useState<GameRow | null>(null);

    // Clear selection when the result set changes (e.g. new query in QueryView).
    // NOTE: this effect fires when `games` changes by reference. In GamesView,
    // `games` is derived as `data?.games ?? []`. The `?? []` fallback creates a
    // new array every render when data is undefined (during loading), which would
    // fire this effect on every render during loading. This is harmless in practice
    // (no selection exists during loading), but if it causes issues the consuming
    // view should memoize the fallback: `const games = useMemo(() => data?.games ?? [], [data])`.
    useEffect(() => {
      setSelectedGame(null);
    }, [games]);

    return (
      <GameTable
        games={games}
        sortBy={sortBy}
        sortDir={sortDir}
        onSort={onSort}
        onRowClick={(game) =>
          setSelectedGame((prev) => (prev?.gameUrl === game.gameUrl ? null : game))
        }
        selectedGame={selectedGame}
        onClose={() => setSelectedGame(null)}
      />
    );
  }
  ```

- [ ] **Step 4: Run tests to confirm they all pass**

  ```bash
  npx vitest run src/__tests__/GameResultsTable.test.tsx
  ```

  Expected: all 6 tests PASS

- [ ] **Step 5: Run full test suite to confirm no regressions**

  ```bash
  npm test
  ```

  Expected: all tests PASS

- [ ] **Step 6: Commit**

  ```bash
  git add src/components/GameResultsTable.tsx src/__tests__/GameResultsTable.test.tsx
  git commit -m "feat(1d4_web): add GameResultsTable with accordion selection state"
  ```

---

## Chunk 2: Update views to use GameResultsTable

### Task 3: Update GamesView

**Files:**
- Modify: `src/views/GamesView.tsx`

- [ ] **Step 1: Replace GameTable usage with GameResultsTable in GamesView**

  In `src/views/GamesView.tsx`:

  1. Change the import — replace:
     ```tsx
     import GameTable from '../components/GameTable';
     ```
     with:
     ```tsx
     import GameResultsTable from '../components/GameResultsTable';
     ```

  2. Remove the `selectedGame` state declaration:
     ```tsx
     // DELETE this line:
     const [selectedGame, setSelectedGame] = useState<GameRow | null>(null);
     ```

  3. Replace the entire `{!isLoading && !error && (` block with:
     ```tsx
     {!isLoading && !error && (
       <>
         <GameResultsTable
           games={sorted}
           sortBy={sortBy}
           sortDir={sortDir}
           onSort={handleSort}
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
     ```
     The `<Pagination>` component is unchanged — it moves from the old block into the new one. The `onRowClick`, `selectedGame`, and `onClose` props are gone — those are now internal to `GameResultsTable`.

  4. Remove the unused `useState` import if `selectedGame` was the only state using it — but check first; `usernameInput`, `username`, `sortBy`, `sortDir`, `page`, `pageSize` all use it so `useState` stays.

- [ ] **Step 2: Run GamesView tests**

  ```bash
  npx vitest run src/__tests__/GamesView.test.tsx
  ```

  Expected: all 7 tests PASS (accordion behavior is unchanged; the "opens game detail panel when a row is clicked" test still works because `GameResultsTable` handles it internally).

- [ ] **Step 3: Commit**

  ```bash
  git add src/views/GamesView.tsx
  git commit -m "refactor(1d4_web): GamesView uses GameResultsTable"
  ```

---

### Task 4: Update QueryView

**Files:**
- Modify: `src/views/QueryView.tsx`

- [ ] **Step 1: Replace GameTable + standalone GameDetailPanel with GameResultsTable in QueryView**

  In `src/views/QueryView.tsx`:

  1. Replace imports — remove:
     ```tsx
     import GameTable from '../components/GameTable';
     import GameDetailPanel from '../components/GameDetailPanel';
     ```
     Add:
     ```tsx
     import GameResultsTable from '../components/GameResultsTable';
     ```

  2. Remove the `selectedGame` state declaration:
     ```tsx
     // DELETE this line:
     const [selectedGame, setSelectedGame] = useState<GameRow | null>(null);
     ```

  3. In the `{!isLoading && !error && games.length > 0 && (` block, replace:
     ```tsx
     <>
       <GameTable
         games={games}
         sortBy=""
         sortDir="asc"
         onSort={() => {}}
         onRowClick={(game) => setSelectedGame(game)}
       />
       <p className="empty" style={{ textAlign: 'left' }}>
         Showing {games.length} result(s).
       </p>
       {selectedGame && (
         <GameDetailPanel
           key={selectedGame.gameUrl}
           game={selectedGame}
           onClose={() => setSelectedGame(null)}
         />
       )}
     </>
     ```
     with:
     ```tsx
     <>
       <GameResultsTable games={games} />
       <p className="empty" style={{ textAlign: 'left' }}>
         Showing {games.length} result(s).
       </p>
     </>
     ```

  4. Remove unused `useState` import if `selectedGame` was the only state — verify: `queryText`, `committedQuery`, `limit`, `selectedGame` all used `useState`. After removing `selectedGame`, the other three still use it, so `useState` stays.

  5. Remove unused `GameRow` type import if it was only used for `selectedGame` — check: it's also used in the `useQuery` generic and the `games` variable typing so it likely stays. Verify by checking if TypeScript complains.

- [ ] **Step 2: Run QueryView tests**

  ```bash
  npx vitest run src/__tests__/QueryView.test.tsx
  ```

  Expected: all 7 tests PASS. The "opens game detail panel when a result row is clicked" test (line 99) clicks a row and checks for "Alice vs Bob" — this still works because `GameResultsTable` handles the accordion internally.

- [ ] **Step 3: Run full test suite**

  ```bash
  npm test
  ```

  Expected: all tests PASS

- [ ] **Step 4: Commit**

  ```bash
  git add src/views/QueryView.tsx
  git commit -m "refactor(1d4_web): QueryView uses GameResultsTable, game detail now opens accordion style"
  ```

---

## Done

After all tasks complete, both `GamesView` and `QueryView` render game details as an inline accordion row. The selection state lives in `GameResultsTable` and is not duplicated. `GameTable` remains a pure presentational component.
