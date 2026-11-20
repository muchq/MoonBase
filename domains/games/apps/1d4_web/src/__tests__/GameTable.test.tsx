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
    platform: 'CHESS_COM',
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
    gameUrl: 'https://lichess.org/game2',
    platform: 'LICHESS',
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
      fork: [{ gameUrl: 'https://lichess.org/game2', motif: 'fork', moveNumber: 20, side: 'white', description: 'Fork' }],
      checkmate: [{ gameUrl: 'https://lichess.org/game2', motif: 'checkmate', moveNumber: 55, side: 'white', description: 'Checkmate' }],
    },
  },
];

describe('GameTable', () => {
  it('renders a row for each game', () => {
    render(<GameTable games={mockGames} sortBy="" sortDir="asc" onSort={() => {}} />);
    expect(screen.getByText('Alice')).toBeInTheDocument();
    expect(screen.getByText('Carol')).toBeInTheDocument();
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
    fireEvent.click(screen.getByText('Motifs'));
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
    // 1700000000 seconds = 2023-11-14 (mockGames[0].playedAt)
    expect(screen.getByText('2023-11-14')).toBeInTheDocument();
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

  // The whole point of the column set is that it is short. Without this,
  // re-adding a Game or Indexed column — the two that crushed Motifs — is a
  // silently green change. Asserted as an exact list rather than two absence
  // checks so any twelfth column has to be a deliberate edit here too.
  it('shows only the columns that earn their width', () => {
    render(<GameTable games={mockGames} />);
    const headers = Array.from(
      screen.getByRole('table').querySelectorAll('thead th')
    ).map((th) => th.textContent);
    expect(headers).toEqual([
      'Site',
      'White',
      'Black',
      'Time',
      'ECO',
      'Result',
      'Played',
      'Motifs',
    ]);
  });

  // The negative half of the move: the link and the indexed date left the table
  // for GameDetailPanel, so the table body must hold no link at all. The way
  // back to the game is the White-cell toggle plus the panel, covered below.
  it('renders no links in the table body — the game link lives in the detail panel', () => {
    render(<GameTable games={mockGames} />);
    expect(screen.queryAllByRole('link')).toHaveLength(0);
  });

  // A <tr onClick> is not focusable and has no keyboard semantics, so when the
  // game link left the table the body had no keyboard affordance at all and
  // the panel became pointer-only. These pin the button that fixed that.
  it('renders the White cell as a button that reports its collapsed state', () => {
    render(<GameTable games={mockGames} onRowClick={() => {}} />);
    const toggle = screen.getByRole('button', { name: 'Alice' });
    expect(toggle).toHaveAttribute('aria-expanded', 'false');
    // Dangling references are worse than none: no panel exists to point at.
    expect(toggle).not.toHaveAttribute('aria-controls');
  });

  it('reports the expanded state and points at the panel it opened', () => {
    render(
      <GameTable
        games={mockGames}
        onRowClick={() => {}}
        selectedGame={mockGames[0]}
        onClose={() => {}}
      />
    );
    const toggle = screen.getByRole('button', { name: 'Alice' });
    expect(toggle).toHaveAttribute('aria-expanded', 'true');
    const panelId = toggle.getAttribute('aria-controls');
    expect(panelId).toBeTruthy();
    // The id has to resolve, or a screen reader follows it nowhere.
    expect(document.getElementById(panelId!)).not.toBeNull();
  });

  // The row's own onClick still fires for mouse users. Without stopPropagation
  // on the button both handlers run, the toggle flips twice, and the panel
  // lands back exactly where it started — a click that visibly does nothing.
  // The existing "calls onRowClick with the correct game" test cannot see this:
  // toHaveBeenCalledWith passes just as well when called twice.
  it('toggles exactly once when the White button is clicked', () => {
    const onRowClick = vi.fn();
    render(<GameTable games={mockGames} onRowClick={onRowClick} />);
    fireEvent.click(screen.getByRole('button', { name: 'Alice' }));
    expect(onRowClick).toHaveBeenCalledTimes(1);
    expect(onRowClick).toHaveBeenCalledWith(mockGames[0]);
  });

  // Non-expandable tables must not grow a dead control.
  it('leaves the White cell as plain text when the table cannot expand', () => {
    render(<GameTable games={mockGames} />);
    expect(screen.queryByRole('button', { name: 'Alice' })).not.toBeInTheDocument();
    expect(screen.getByText('Alice')).toBeInTheDocument();
  });

  // jsdom has no layout, so this cannot assert the rendered result — only that
  // the hook the stylesheet hangs the fix on is still here. The columns do not
  // fit a phone, and without the modifier the browser keeps honouring
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
    // Anchored to a line start so `.motifs` cannot match inside `td .motifs`.
    const rule = (selector: string) =>
      css.match(
        new RegExp(
          `^${selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\s*\\{([^}]*)\\}`,
          'm'
        )
      )?.[1] ?? '';

    // Not just `\d`: that matched `0` and `4px` as happily as `52rem`, so it
    // could not tell a real floor from a zeroed or unit-less one. Pin the unit
    // and a lower bound instead. The band is wide on purpose — the exact number
    // is a judgement call, but a floor below the table's own min-content
    // (measured 816px with short usernames) is inert, and one far above it
    // reintroduces the laptop scrolling this modifier exists to bound.
    const minWidth = rule('.table-wrap--wide table').match(
      /min-width:\s*(\d+(?:\.\d+)?)rem/
    );
    expect(minWidth).not.toBeNull();
    expect(Number(minWidth![1])).toBeGreaterThanOrEqual(48);
    expect(Number(minWidth![1])).toBeLessThanOrEqual(64);
    expect(rule('.table-wrap--wide td')).toMatch(/white-space:\s*nowrap/);
    // One line per row however many motifs a game has: a wrapping badge list
    // makes row height track motif count.
    expect(rule('.motifs')).toMatch(/flex-wrap:\s*nowrap/);
    // And one width however long the names are: the cell is capped, badges
    // give way, and the "+N" counter does not.
    expect(rule('td .motifs')).toMatch(/max-width:\s*\d/);
    expect(rule('td .motifs .motif-badge')).toMatch(/text-overflow:\s*ellipsis/);
    expect(rule('td .motifs .motif-more')).toMatch(/flex-shrink:\s*0/);
    // The wrapper has to be the thing that scrolls, or the min-width just
    // overflows the page and the columns are off-screen instead of squeezed.
    expect(rule('.table-wrap')).toMatch(/overflow-x:\s*auto/);
  });
});

// #1540: platform reached the client and no component rendered it. Harmless
// with one platform; at two, usernames do not carry across sites, so two
// people sharing a handle produce rows identical on screen.
describe('platform', () => {
  it('says which site each row came from', () => {
    render(<GameTable games={mockGames} />);

    const rows = screen.getAllByRole('row').slice(1);
    expect(rows[0]).toHaveTextContent('chess.com');
    expect(rows[1]).toHaveTextContent('lichess');
  });

  it('shows the site name rather than the stored spelling', () => {
    render(<GameTable games={mockGames} />);

    expect(screen.queryByText('CHESS_COM')).toBeNull();
    expect(screen.queryByText('LICHESS')).toBeNull();
  });
});

// Ratings ride along in the player cells rather than spending two columns.
describe('ratings', () => {
  it("shows each player's rating beside their name", () => {
    render(<GameTable games={mockGames} />);
    const row = screen.getAllByRole('row')[1];
    expect(row).toHaveTextContent('Alice1800');
    expect(row).toHaveTextContent('Bob1750');
  });

  // Unrated games (muchq.com) store 0; a column of zeros says nothing.
  it('omits a rating of zero or none', () => {
    const unrated = { ...mockGames[0], whiteElo: 0, blackElo: null };
    render(<GameTable games={[unrated]} />);
    const cells = screen.getAllByRole('row')[1].querySelectorAll('td');
    expect(cells[1]).toHaveTextContent(/^Alice$/);
    expect(cells[2]).toHaveTextContent(/^Bob$/);
  });

  // The rating sits outside the toggle so the button's name stays the player.
  it('keeps the rating out of the expand button', () => {
    render(<GameTable games={mockGames} onRowClick={() => {}} />);
    expect(screen.getByRole('button', { name: 'Alice' })).toBeInTheDocument();
  });
});

describe('motif overflow', () => {
  const many = ['fork', 'pin', 'skewer', 'checkmate', 'double_check'];
  const game: GameRow = {
    ...mockGames[0],
    occurrences: Object.fromEntries(
      many.map((motif) => [
        motif,
        [{ gameUrl: 'g', motif, moveNumber: 1, side: 'white', description: motif }],
      ])
    ),
  };

  it('shows two badges and counts the rest', () => {
    render(<GameTable games={[game]} />);
    expect(screen.getByText('fork')).toBeInTheDocument();
    expect(screen.getByText('pin')).toBeInTheDocument();
    expect(screen.queryByText('skewer')).toBeNull();
    const more = screen.getByText('+3');
    expect(more).toHaveClass('motif-more');
    expect(more).toHaveAttribute('title', 'skewer, checkmate, double check');
  });

  it('shows no counter when every motif fits', () => {
    render(<GameTable games={mockGames} />);
    expect(screen.queryByText(/^\+\d+$/)).toBeNull();
  });
});
