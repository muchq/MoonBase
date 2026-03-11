import { Fragment } from 'react';
import type { GameRow } from '../types';
import MotifBadge from './MotifBadge';
import GameDetailPanel from './GameDetailPanel';


const COLUMNS = [
  { id: 'gameUrl', label: 'Game', sort: false },
  { id: 'whiteUsername', label: 'White', sort: true },
  { id: 'blackUsername', label: 'Black', sort: true },
  { id: 'whiteElo', label: 'White ELO', sort: true },
  { id: 'blackElo', label: 'Black ELO', sort: true },
  { id: 'timeClass', label: 'Time', sort: true },
  { id: 'eco', label: 'ECO', sort: true },
  { id: 'result', label: 'Result', sort: true },
  { id: 'playedAt', label: 'Played', sort: true },
  { id: 'indexedAt', label: 'Indexed', sort: true },
  { id: 'motifs', label: 'Motifs', sort: false },
];

function formatElo(v: number | null): string {
  return v != null ? String(v) : '—';
}

function formatDate(val: string | number | null | undefined): string {
  if (val == null) return '—';
  const date = typeof val === 'number' ? new Date(val * 1000) : new Date(val);
  return isNaN(date.getTime()) ? '—' : date.toISOString().slice(0, 10);
}

function renderCell(
  colId: string,
  game: GameRow,
  _onRowClick?: (g: GameRow) => void
): React.ReactNode {
  const g = game as unknown as Record<string, unknown>;
  switch (colId) {
    case 'gameUrl':
      return game.gameUrl ? (
        <a
          href={game.gameUrl}
          target="_blank"
          rel="noopener noreferrer"
          className="external"
          onClick={(e) => e.stopPropagation()}
        >
          View
        </a>
      ) : (
        '—'
      );
    case 'motifs':
      return (
        <span className="motifs">
          {Object.keys(game.occurrences ?? {}).map((label) => (
            <MotifBadge
              key={label}
              label={label}
              occurrences={game.occurrences?.[label]}
            />
          ))}
        </span>
      );
    case 'whiteElo':
    case 'blackElo':
      return formatElo(g[colId] as number | null);
    case 'playedAt':
    case 'indexedAt':
      return formatDate(g[colId] as string | number | null);
    default:
      return String(g[colId] ?? '—');
  }
}

interface Props {
  games: GameRow[];
  sortBy: string;
  sortDir: 'asc' | 'desc';
  onSort: (col: string) => void;
  onRowClick?: (game: GameRow) => void;
  selectedGame?: GameRow | null;
  onClose?: () => void;
}

export default function GameTable({ games, sortBy, sortDir, onSort, onRowClick, selectedGame, onClose }: Props) {
  return (
    <div className="table-wrap">
      <table>
        <thead>
          <tr>
            {COLUMNS.map((col) => (
              <th
                key={col.id}
                className={
                  col.sort && sortBy === col.id ? `sorted-${sortDir}` : ''
                }
                onClick={col.sort ? () => onSort(col.id) : undefined}
                style={{ cursor: col.sort ? 'pointer' : 'default' }}
              >
                {col.label}
                {col.sort && <span className="sort-icon" />}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {games.map((game, i) => (
            <Fragment key={game.gameUrl || i}>
              <tr
                onClick={onRowClick ? () => onRowClick(game) : undefined}
                style={{ cursor: onRowClick ? 'pointer' : 'default' }}
                className={selectedGame?.gameUrl === game.gameUrl ? 'selected' : undefined}
              >
                {COLUMNS.map((col) => (
                  <td key={col.id}>{renderCell(col.id, game, onRowClick)}</td>
                ))}
              </tr>
              {selectedGame?.gameUrl === game.gameUrl && (
                <tr>
                  <td colSpan={COLUMNS.length} style={{ padding: 0 }}>
                    <GameDetailPanel
                      game={selectedGame}
                      onClose={onClose ?? (() => {})}
                    />
                  </td>
                </tr>
              )}
            </Fragment>
          ))}
        </tbody>
      </table>
    </div>
  );
}
