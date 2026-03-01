import React from 'react';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router-dom';
import QueryView from '../views/QueryView';
import * as api from '../api';
import type { GameRow } from '../types';

vi.mock('../api');
vi.mock('react-chessboard', () => ({
  Chessboard: ({ position }: { position: string }) => (
    <div data-testid="chessboard" data-fen={position} />
  ),
}));

const mockGame: GameRow = {
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
  hasOverloadedPiece: false,
  hasZugzwang: false,
};

function makeWrapper() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return function Wrapper({ children }: { children: React.ReactNode }) {
    return (
      <QueryClientProvider client={qc}>
        <MemoryRouter>{children}</MemoryRouter>
      </QueryClientProvider>
    );
  };
}

describe('QueryView', () => {
  beforeEach(() => {
    vi.mocked(api.query).mockResolvedValue({ games: [], count: 0 });
  });

  it('sets textarea value when an example chip is clicked', () => {
    render(<QueryView />, { wrapper: makeWrapper() });
    // Use role query to target the chip button specifically (not the <code> in syntax help)
    fireEvent.click(screen.getByRole('button', { name: 'motif(fork)' }));
    const textarea = screen.getByPlaceholderText(
      'e.g. motif(fork) AND white.elo >= 2500'
    );
    expect((textarea as HTMLTextAreaElement).value).toBe('motif(fork)');
  });

  it('calls api.query when Run query is clicked', async () => {
    render(<QueryView />, { wrapper: makeWrapper() });
    const textarea = screen.getByPlaceholderText(
      'e.g. motif(fork) AND white.elo >= 2500'
    );
    fireEvent.change(textarea, { target: { value: 'motif(skewer)' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run query' }));

    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({ query: 'motif(skewer)' })
      )
    );
  });

  it('shows no-results message for empty response', async () => {
    render(<QueryView />, { wrapper: makeWrapper() });
    fireEvent.change(
      screen.getByPlaceholderText('e.g. motif(fork) AND white.elo >= 2500'),
      { target: { value: 'eco = "Z99"' } }
    );
    fireEvent.click(screen.getByRole('button', { name: 'Run query' }));
    await waitFor(() =>
      expect(screen.getByText(/No results/)).toBeInTheDocument()
    );
  });

  it('renders results table when query returns games', async () => {
    vi.mocked(api.query).mockResolvedValue({ games: [mockGame], count: 1 });
    render(<QueryView />, { wrapper: makeWrapper() });
    fireEvent.change(
      screen.getByPlaceholderText('e.g. motif(fork) AND white.elo >= 2500'),
      { target: { value: 'motif(fork)' } }
    );
    fireEvent.click(screen.getByRole('button', { name: 'Run query' }));
    await waitFor(() =>
      expect(screen.getByText('Showing 1 result(s).')).toBeInTheDocument()
    );
    expect(screen.getByText('Alice')).toBeInTheDocument();
  });

  it('opens game detail panel when a result row is clicked', async () => {
    vi.mocked(api.query).mockResolvedValue({ games: [mockGame], count: 1 });
    render(<QueryView />, { wrapper: makeWrapper() });
    fireEvent.change(
      screen.getByPlaceholderText('e.g. motif(fork) AND white.elo >= 2500'),
      { target: { value: 'motif(fork)' } }
    );
    fireEvent.click(screen.getByRole('button', { name: 'Run query' }));
    await waitFor(() => screen.getByText('Alice'));
    const rows = screen.getAllByRole('row');
    fireEvent.click(rows[1]);
    expect(screen.getByText('Alice vs Bob')).toBeInTheDocument();
  });

  it('does not fire a query when Run query is clicked with empty text', async () => {
    render(<QueryView />, { wrapper: makeWrapper() });
    fireEvent.click(screen.getByRole('button', { name: 'Run query' }));
    await new Promise((r) => setTimeout(r, 50));
    expect(api.query).not.toHaveBeenCalled();
  });
});
