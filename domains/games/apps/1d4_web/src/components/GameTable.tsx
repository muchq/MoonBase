import { Fragment, useId, useRef } from 'react';
import type { GameRow } from '../types';
import MotifBadge from './MotifBadge';
import GameDetailPanel from './GameDetailPanel';


// The game link and indexed-at timestamp live in GameDetailPanel instead of
// here — eleven columns left the motifs column, the one that actually needs
// room, squeezed to nothing on a laptop-width screen. Click the row to get
// both back.
const COLUMNS = [
  { id: 'whiteUsername', label: 'White', sort: true },
  { id: 'blackUsername', label: 'Black', sort: true },
  { id: 'whiteElo', label: 'White ELO', sort: true },
  { id: 'blackElo', label: 'Black ELO', sort: true },
  { id: 'timeClass', label: 'Time', sort: true },
  { id: 'eco', label: 'ECO', sort: true },
  { id: 'result', label: 'Result', sort: true },
  { id: 'playedAt', label: 'Played', sort: true },
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

/** How the White cell expands its row. Absent when the table isn't expandable. */
interface RowToggle {
  onToggle: () => void;
  isExpanded: boolean;
  panelId: string;
  register: (el: HTMLButtonElement | null) => void;
}

function renderCell(
  colId: string,
  game: GameRow,
  toggle: RowToggle | null
): React.ReactNode {
  const g = game as unknown as Record<string, unknown>;
  switch (colId) {
    // The White cell doubles as the row's expand control. The row's own
    // onClick stays for mouse users, but a <tr> is not focusable and has no
    // keyboard semantics, so before this the detail panel — and with it the
    // only link to the game on chess.com — could not be reached without a
    // pointer. A real <button> buys Enter/Space and expand/collapse state for
    // free, and costs no column width.
    case 'whiteUsername':
      return toggle ? (
        <button
          type="button"
          className="row-toggle"
          ref={toggle.register}
          aria-expanded={toggle.isExpanded}
          // Only while the panel exists: aria-controls pointing at an absent
          // id is a dangling reference.
          aria-controls={toggle.isExpanded ? toggle.panelId : undefined}
          onClick={(e) => {
            // Without this the row handler fires too and toggles a second
            // time, landing back exactly where it started.
            e.stopPropagation();
            toggle.onToggle();
          }}
        >
          {game.whiteUsername}
        </button>
      ) : (
        game.whiteUsername
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
      return formatDate(g[colId] as string | number | null);
    default:
      return String(g[colId] ?? '—');
  }
}

interface Props {
  games: GameRow[];
  sortBy?: string;
  sortDir?: 'asc' | 'desc';
  onSort?: (col: string) => void;
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
  // Unique per mounted table, so two tables on one page can't mint the same
  // panel id for aria-controls to point at.
  const idPrefix = useId();
  const toggleRefs = useRef(new Map<string, HTMLButtonElement | null>());
  const panelIdFor = (game: GameRow, i: number) =>
    `${idPrefix}panel-${game.gameUrl || i}`;

  // Closing returns focus to the button that opened the panel. Without it the
  // × unmounts the focused element and the keyboard user is dropped back to
  // the top of the document, which is its own way of being unusable.
  function handleClose() {
    if (selectedGame) toggleRefs.current.get(selectedGame.gameUrl)?.focus();
    onClose?.();
  }

  return (
    <div className="table-wrap table-wrap--wide">
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
          {games.map((game, i) => {
            const isExpanded = selectedGame?.gameUrl === game.gameUrl;
            const panelId = panelIdFor(game, i);
            const toggle: RowToggle | null = onRowClick
              ? {
                  onToggle: () => onRowClick(game),
                  isExpanded,
                  panelId,
                  register: (el) => toggleRefs.current.set(game.gameUrl, el),
                }
              : null;
            return (
              <Fragment key={game.gameUrl || i}>
                <tr
                  onClick={onRowClick ? () => onRowClick(game) : undefined}
                  style={{ cursor: onRowClick ? 'pointer' : 'default' }}
                  className={isExpanded ? 'selected' : undefined}
                >
                  {COLUMNS.map((col) => (
                    <td key={col.id}>{renderCell(col.id, game, toggle)}</td>
                  ))}
                </tr>
                {isExpanded && selectedGame && (
                  <tr id={panelId}>
                    <td colSpan={COLUMNS.length} style={{ padding: 0 }}>
                      <GameDetailPanel game={selectedGame} onClose={handleClose} />
                    </td>
                  </tr>
                )}
              </Fragment>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
