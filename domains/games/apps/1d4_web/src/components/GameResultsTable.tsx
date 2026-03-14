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
