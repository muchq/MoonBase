import { render, screen, fireEvent } from '@testing-library/react';
import { describe, it, expect, vi } from 'vitest';
import GameTable from '../components/GameTable';
import type { GameRow } from '../types';

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
    hasPin: true,
    hasCrossPin: false,
    hasFork: false,
    hasSkewer: false,
    hasDiscoveredAttack: false,
    hasDiscoveredCheck: false,
    hasCheck: false,
    hasCheckmate: false,
    hasPromotion: false,
    hasPromotionWithCheck: false,
    hasPromotionWithCheckmate: false,

    hasDoubleCheck: false,
    hasBackRankMate: false,
    hasSmotheredMate: false,
    hasOverloadedPiece: false,
    hasZugzwang: false,
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
    hasPin: false,
    hasCrossPin: false,
    hasFork: true,
    hasSkewer: false,
    hasDiscoveredAttack: false,
    hasDiscoveredCheck: false,
    hasCheck: false,
    hasCheckmate: true,
    hasPromotion: false,
    hasPromotionWithCheck: false,
    hasPromotionWithCheckmate: false,

    hasDoubleCheck: false,
    hasBackRankMate: false,
    hasSmotheredMate: false,
    hasOverloadedPiece: false,
    hasZugzwang: false,
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
});
