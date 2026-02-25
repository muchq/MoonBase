import { useState, useMemo } from 'react';
import { useQuery } from '@tanstack/react-query';
import { query as apiQuery } from '../api';
import type { GameRow } from '../types';
import GameTable from '../components/GameTable';
import GameDetailPanel from '../components/GameDetailPanel';
import Pagination from '../components/Pagination';

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
  const [offset, setOffset] = useState(0);
  const [limit, setLimit] = useState(25);
  const [selectedGame, setSelectedGame] = useState<GameRow | null>(null);

  const queryText = buildQuery(username);
  const { data, isLoading, error } = useQuery({
    queryKey: ['games', queryText],
    queryFn: () => apiQuery({ query: queryText, limit: 500, offset: 0 }),
  });

  const games = data?.games ?? [];
  const sorted = useMemo(
    () => sortGames(games, sortBy, sortDir),
    [games, sortBy, sortDir]
  );
  const page = sorted.slice(offset, offset + limit);

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
    setOffset(0);
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
            games={page}
            sortBy={sortBy}
            sortDir={sortDir}
            onSort={handleSort}
            onRowClick={(game) => setSelectedGame(game)}
          />
          <Pagination
            offset={offset}
            limit={limit}
            total={sorted.length}
            onLimitChange={(n) => {
              setLimit(n);
              setOffset(0);
            }}
            onPrev={() => setOffset((o) => Math.max(0, o - limit))}
            onNext={() =>
              setOffset((o) => Math.min(o + limit, sorted.length - limit))
            }
          />
          {selectedGame && (
            <GameDetailPanel
              key={selectedGame.gameUrl}
              game={selectedGame}
              onClose={() => setSelectedGame(null)}
            />
          )}
        </>
      )}
    </>
  );
}
