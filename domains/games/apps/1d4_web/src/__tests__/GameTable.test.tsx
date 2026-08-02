import { render, screen, fireEvent } from '@testing-library/react';
import { readFileSync } from 'node:fs';
import { describe, it, expect, vi } from 'vitest';
import GameTable from '../components/GameTable';
import type { GameRow } from '../types';

vi.mock('react-chessboard', () => ({
  Chessboard: ({ position }: { position: string }) => (
    <div data-testid="chessboard" data-fen={position} />
  ),
}));

const mockGames: GameRow[] = [
  {
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
    occurrences: { pin: [{ gameUrl: 'https://chess.com/game/1', motif: 'pin', moveNumber: 15, side: 'white', description: 'Pin' }] },
  },
  {
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
    occurrences: {
      fork: [{ gameUrl: 'https://chess.com/game/2', motif: 'fork', moveNumber: 20, side: 'white', description: 'Fork' }],
      checkmate: [{ gameUrl: 'https://chess.com/game/2', motif: 'checkmate', moveNumber: 55, side: 'white', description: 'Checkmate' }],
    },
  },
];

describe('GameTable', () => {
  it('renders a row for each game', () => {
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={() => {}} />);
    expect(screen.getByText('Alice')).toBeInTheDocument();
    expect(screen.getByText('Carol')).toBeInTheDocument();
  });

  it('renders external links for game URLs', () => {
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={() => {}} />);
    const links = screen.getAllByRole('link', { name: 'View' });
    expect(links).toHaveLength(2);
    expect(links[0]).toHaveAttribute('href', 'https://chess.com/game/1');
  });

  it('calls onSort when a sortable column header is clicked', () => {
    const onSort = vi.fn();
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={onSort} />);
    fireEvent.click(screen.getByText('White'));
    expect(onSort).toHaveBeenCalledWith('whiteUsername');
  });

  it('does not call onSort for non-sortable columns', () => {
    const onSort = vi.fn();
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={onSort} />);
    fireEvent.click(screen.getByText('Game'));
    expect(onSort).not.toHaveBeenCalled();
  });

  it('renders motif badges for games with motifs', () => {
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={() => {}} />);
    expect(screen.getByText('pin')).toBeInTheDocument();
    expect(screen.getByText('fork')).toBeInTheDocument();
    expect(screen.getByText('checkmate')).toBeInTheDocument();
  });

  it('calls onRowClick with the correct game when a row is clicked', () => {
    const onRowClick = vi.fn();
    render(
      <GameTable
        games={mockGames}
        sortBy=""
        sortDir="asc"
        onSort={() => {}}
        onRowClick={onRowClick}
      />
    );
    fireEvent.click(screen.getByText('Alice'));
    expect(onRowClick).toHaveBeenCalledWith(mockGames[0]);
  });

  it('formats unix timestamps as YYYY-MM-DD dates', () => {
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={() => {}} />);
    // 1700000000 seconds = 2023-11-14 (both playedAt and indexedAt land on same day)
    const dateCells = screen.getAllByText('2023-11-14');
    expect(dateCells.length).toBeGreaterThanOrEqual(1);
  });

  it('shows game detail accordion inline when selectedGame matches a row', () => {
    render(
      <GameTable
        games={mockGames}
        sortBy=""
        sortDir="asc"
        onSort={() => {}}
        selectedGame={mockGames[0]}
        onClose={() => {}}
      />
    );
    expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
  });

  it('does not show game detail accordion when no game is selected', () => {
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={() => {}} />);
    expect(screen.queryByTestId('chessboard')).not.toBeInTheDocument();
  });

  it('renders correctly when sort props are omitted (uses defaults)', () => {
    render(<GameTable games={mockGames} />);
    expect(screen.getByText('Alice')).toBeInTheDocument();
    expect(screen.getByText('Carol')).toBeInTheDocument();
  });

  // jsdom has no layout, so this cannot assert the rendered result — only that
  // the hook the stylesheet hangs the fix on is still here. Eleven columns do
  // not fit a laptop, and without the modifier the browser keeps honouring
  // width:100% by crushing the last ones instead of letting the wrapper scroll:
  // ISO dates break after the month and motif badges split mid-word.
  it('marks the games table as wide so its columns scroll instead of being crushed', () => {
    const { container } = render(<GameTable games={mockGames} />);
    const wrap = container.querySelector('.table-wrap');
    expect(wrap).not.toBeNull();
    expect(wrap).toHaveClass('table-wrap--wide');
  });

  // The other half. A class name with no rule behind it is a passing test and a
  // clipped table, and the assertion above cannot tell the two apart — so this
  // reads the stylesheet directly. Not a layout test: it says the three
  // declarations the modifier exists for are still declared, which is the most
  // jsdom's absent layout engine leaves available.
  it('backs that class with the rules that make it mean something', () => {
    // Read off disk, not imported: vitest stubs CSS imports, and `?raw` comes back
    // empty here — which would make every assertion below pass against an empty
    // string. process.cwd() is the vitest root, i.e. this package.
    const css = readFileSync('src/index.css', 'utf8');
    expect(css).not.toHaveLength(0);
    const rule = (selector: string) =>
      css.match(new RegExp(`\\${selector}\\s*\\{([^}]*)\\}`))?.[1] ?? '';

    expect(rule('.table-wrap--wide table')).toMatch(/min-width:\s*\d/);
    expect(rule('.table-wrap--wide td')).toMatch(/white-space:\s*nowrap/);
    // Badges wrap between themselves; only mid-badge breaking is the bug.
    expect(rule('.table-wrap--wide td .motifs')).toMatch(/white-space:\s*normal/);
    // The wrapper has to be the thing that scrolls, or the min-width just
    // overflows the page and the columns are off-screen instead of squeezed.
    expect(rule('.table-wrap')).toMatch(/overflow-x:\s*auto/);
  });
});
