import React from 'react';
import { render, screen, fireEvent } from '@testing-library/react';
import { describe, it, expect, vi } from 'vitest';
import GameDetailPanel from '../components/GameDetailPanel';
import type { GameRow, OccurrenceRow } from '../types';

vi.mock('react-chessboard', () => ({
  Chessboard: ({ position }: { position: string }) => (
    <div data-testid="chessboard" data-fen={position} />
  ),
}));

// 1. e4 e5 2. Nf3 Nc6 — 4 half-moves
const TEST_PGN = '1. e4 e5 2. Nf3 Nc6';

const forkOccurrence: OccurrenceRow = {
  gameUrl: 'https://chess.com/game/1',
  motif: 'fork',
  moveNumber: 2,
  side: 'white',
  description: 'Fork detected',
};

const mockGame: GameRow = {
  gameUrl: 'https://chess.com/game/1',
  platform: 'chess.com',
  whiteUsername: 'Alice',
  blackUsername: 'Bob',
  whiteElo: 1500,
  blackElo: 1480,
  timeClass: 'blitz',
  eco: 'C60',
  result: '1-0',
  playedAt: 1700000000,
  indexedAt: 1700001000,
  numMoves: 4,
  hasPin: false,
  hasCrossPin: false,
  hasFork: true,
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
  hasSacrifice: false,
  hasInterference: false,
  hasOverloadedPiece: false,
  hasZugzwang: false,
  pgn: TEST_PGN,
  occurrences: { fork: [forkOccurrence] },
};

describe('GameDetailPanel', () => {
  it('renders player names and game info', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
    expect(screen.getByText(/1-0/)).toBeInTheDocument();
  });

  it('calls onClose when close button is clicked', () => {
    const onClose = vi.fn();
    render(<GameDetailPanel game={mockGame} onClose={onClose} />);
    fireEvent.click(screen.getByRole('button', { name: 'Close panel' }));
    expect(onClose).toHaveBeenCalledOnce();
  });

  it('shows chessboard when pgn is present', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    expect(screen.getByTestId('chessboard')).toBeInTheDocument();
  });

  it('shows PGN not available message when pgn is absent', () => {
    const game = { ...mockGame, pgn: undefined };
    render(<GameDetailPanel game={game} onClose={() => {}} />);
    expect(screen.getByText(/PGN not available/)).toBeInTheDocument();
    expect(screen.queryByTestId('chessboard')).not.toBeInTheDocument();
  });

  it('shows start position initially', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    expect(screen.getByText('Start')).toBeInTheDocument();
  });

  it('prev button is disabled at start', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    expect(screen.getByRole('button', { name: 'Previous move' })).toBeDisabled();
  });

  it('advances to next move and updates position', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    const initialFen = screen.getByTestId('chessboard').getAttribute('data-fen');
    fireEvent.click(screen.getByRole('button', { name: 'Next move' }));
    const newFen = screen.getByTestId('chessboard').getAttribute('data-fen');
    expect(newFen).not.toBe(initialFen);
    expect(screen.getByText('Move 1 (W)')).toBeInTheDocument();
  });

  it('next button is disabled at end', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    const nextBtn = screen.getByRole('button', { name: 'Next move' });
    // Advance through all 4 half-moves
    fireEvent.click(nextBtn);
    fireEvent.click(nextBtn);
    fireEvent.click(nextBtn);
    fireEvent.click(nextBtn);
    expect(nextBtn).toBeDisabled();
    expect(screen.getByText('End')).toBeInTheDocument();
  });

  it('go-to-start resets to initial position', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: 'Next move' }));
    fireEvent.click(screen.getByRole('button', { name: 'Next move' }));
    fireEvent.click(screen.getByRole('button', { name: 'Go to start' }));
    expect(screen.getByText('Start')).toBeInTheDocument();
  });

  it('go-to-end seeks to last position', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    fireEvent.click(screen.getByRole('button', { name: 'Go to end' }));
    expect(screen.getByText('End')).toBeInTheDocument();
  });

  it('lists motif occurrences with label and description', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    expect(screen.getByText('fork')).toBeInTheDocument();
    expect(screen.getByText('2.')).toBeInTheDocument();
    expect(screen.getByText('Fork detected')).toBeInTheDocument();
  });

  it('clicking an occurrence seeks to the motif position', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    const startFen = screen.getByTestId('chessboard').getAttribute('data-fen');
    // forkOccurrence: moveNumber=2, side=white → ply=2, seekTo(3)
    fireEvent.click(screen.getByText('Fork detected'));
    const newFen = screen.getByTestId('chessboard').getAttribute('data-fen');
    expect(newFen).not.toBe(startFen);
    expect(screen.getByText('Move 2 (W)')).toBeInTheDocument();
  });

  it('shows black occurrence move label with ellipsis', () => {
    const blackOcc: OccurrenceRow = { gameUrl: 'https://chess.com/game/1', motif: 'pin', moveNumber: 1, side: 'black', description: 'Pin' };
    const game = { ...mockGame, occurrences: { pin: [blackOcc] } };
    render(<GameDetailPanel game={game} onClose={() => {}} />);
    expect(screen.getByText('1...')).toBeInTheDocument();
  });

  it('flip board button is present', () => {
    render(<GameDetailPanel game={mockGame} onClose={() => {}} />);
    expect(screen.getByRole('button', { name: 'Flip board' })).toBeInTheDocument();
  });
});
