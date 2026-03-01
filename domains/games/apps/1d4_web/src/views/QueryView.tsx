import { useState } from 'react';
import { useQuery } from '@tanstack/react-query';
import { query as apiQuery } from '../api';
import type { GameRow } from '../types';
import GameTable from '../components/GameTable';
import GameDetailPanel from '../components/GameDetailPanel';

const EXAMPLE_QUERIES = [
  'motif(fork)',
  'white.elo >= 2500 AND motif(pin)',
  'eco = "B90"',
  'motif(back_rank_mate) OR motif(smothered_mate)',
  'motif(skewer) OR motif(discovered_attack)',
  'motif(promotion_with_check) OR motif(promotion_with_checkmate)',
  'time.class = "blitz"',
];

const LIMIT_OPTIONS = [10, 25, 50, 100, 250, 500];

export default function QueryView() {
  const [queryText, setQueryText] = useState('');
  const [committedQuery, setCommittedQuery] = useState('');
  const [limit, setLimit] = useState(50);
  const [selectedGame, setSelectedGame] = useState<GameRow | null>(null);

  const { data, isLoading, error, refetch } = useQuery({
    queryKey: ['query', committedQuery, limit],
    queryFn: () => apiQuery({ query: committedQuery, limit, offset: 0 }),
    enabled: committedQuery.trim().length > 0,
  });

  function handleRun() {
    const q = queryText.trim();
    if (!q) return;
    if (q === committedQuery) {
      void refetch();
    } else {
      setCommittedQuery(q);
    }
  }

  return (
    <>
      <div className="panel">
        <h2>ChessQL query</h2>
        <div className="form-group">
          <textarea
            placeholder="e.g. motif(fork) AND white.elo >= 2500"
            value={queryText}
            onChange={(e) => setQueryText(e.target.value)}
          />
        </div>
        <span style={{ color: 'var(--text-muted)', fontSize: '0.875rem' }}>
          Examples:
        </span>
        <div className="chips">
          {EXAMPLE_QUERIES.map((ex) => (
            <button
              key={ex}
              type="button"
              className="chip"
              onClick={() => setQueryText(ex)}
            >
              {ex}
            </button>
          ))}
        </div>
        <div className="pagination" style={{ marginTop: '0.75rem' }}>
          <select
            value={limit}
            onChange={(e) => setLimit(Number(e.target.value))}
          >
            {LIMIT_OPTIONS.map((n) => (
              <option key={n} value={n}>
                Limit {n}
              </option>
            ))}
          </select>
          <button type="button" className="btn" onClick={handleRun}>
            Run query
          </button>
        </div>
      </div>

      <div className="syntax-help">
        <strong>ChessQL</strong> — Fields: <code>white.elo</code>,{' '}
        <code>black.elo</code>, <code>white.username</code>,{' '}
        <code>black.username</code>, <code>time.class</code>,{' '}
        <code>num.moves</code>, <code>eco</code>, <code>result</code>,{' '}
        <code>platform</code>, <code>game.url</code>, <code>played.at</code>.{' '}
        Motifs: <code>motif(discovered_attack)</code>,{' '}
        <code>motif(discovered_check)</code>, <code>motif(fork)</code>,{' '}
        <code>motif(pin)</code>, <code>motif(cross_pin)</code>,{' '}
        <code>motif(skewer)</code>, <code>motif(check)</code>,{' '}
        <code>motif(checkmate)</code>, <code>motif(double_check)</code>,{' '}
        <code>motif(back_rank_mate)</code>, <code>motif(smothered_mate)</code>,{' '}
        <code>motif(promotion)</code>, <code>motif(promotion_with_check)</code>,{' '}
        <code>motif(promotion_with_checkmate)</code>, <code>motif(overloaded_piece)</code>,{' '}
        <code>motif(zugzwang)</code>. Combine with{' '}
        <code>AND</code>, <code>OR</code>, <code>NOT</code>. Strings in double
        quotes, e.g. <code>eco = &quot;B90&quot;</code>. Do not use SELECT or *.
      </div>

      {error && (
        <div className="message error">{(error as Error).message}</div>
      )}
      {isLoading && <div className="loading">Loading…</div>}

      {!isLoading && !error && data && data.games.length > 0 && (
        <>
          <GameTable
            games={data.games}
            sortBy=""
            sortDir="asc"
            onSort={() => {}}
            onRowClick={(game) => setSelectedGame(game)}
          />
          <p className="empty" style={{ textAlign: 'left' }}>
            Showing {data.games.length} result(s).
          </p>
          {selectedGame && (
            <GameDetailPanel
              key={selectedGame.gameUrl}
              game={selectedGame}
              onClose={() => setSelectedGame(null)}
            />
          )}
        </>
      )}
      {!isLoading && !error && committedQuery && data?.games.length === 0 && (
        <p className="empty">
          No results. Try another query or increase the limit.
        </p>
      )}
    </>
  );
}
