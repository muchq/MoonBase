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
