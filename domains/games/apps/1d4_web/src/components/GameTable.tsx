import type { GameRow } from '../types';
import MotifBadge from './MotifBadge';

const MOTIF_KEYS: [keyof GameRow, string][] = [
  ['hasDiscoveredAttack', 'discovered_attack'],
  ['hasDiscoveredCheck', 'discovered_check'],
  ['hasFork', 'fork'],
  ['hasPin', 'pin'],
  ['hasCrossPin', 'cross_pin'],
  ['hasSkewer', 'skewer'],
  ['hasCheck', 'check'],
  ['hasCheckmate', 'checkmate'],
  ['hasDoubleCheck', 'double_check'],
  ['hasBackRankMate', 'back_rank_mate'],
  ['hasSmotheredMate', 'smothered_mate'],
  ['hasPromotion', 'promotion'],
  ['hasPromotionWithCheck', 'promotion_with_check'],
  ['hasPromotionWithCheckmate', 'promotion_with_checkmate'],
  ['hasSacrifice', 'sacrifice'],
  ['hasInterference', 'interference'],
  ['hasOverloadedPiece', 'overloaded_piece'],
  ['hasZugzwang', 'zugzwang'],
];

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
  onRowClick?: (g: GameRow) => void
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
          {MOTIF_KEYS.filter(([key]) => game[key]).map(([, label]) => (
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
}

export default function GameTable({ games, sortBy, sortDir, onSort, onRowClick }: Props) {
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
            <tr
              key={game.gameUrl || i}
              onClick={onRowClick ? () => onRowClick(game) : undefined}
              style={{ cursor: onRowClick ? 'pointer' : 'default' }}
            >
              {COLUMNS.map((col) => (
                <td key={col.id}>{renderCell(col.id, game, onRowClick)}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
