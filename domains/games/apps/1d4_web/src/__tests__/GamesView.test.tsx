import React from 'react';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { afterEach, describe, it, expect, vi, beforeEach } from 'vitest';
import { cleanup } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router';
import GamesView from '../views/GamesView';
import * as api from '../api';
import type { GameRow } from '../types';

// The first-load request contract shared with the backend: one_d4's FirstPageCacheTest pins its
// Java constants to this same file and FirstPageWarmupTest POSTs it verbatim, so neither side can
// drift from the warmed cache key by editing only its own literal.
// Resolved from the package root (vitest's cwd), because import.meta.url is not a file: URL
// under the jsdom transform.
const FIRST_PAGE_CONTRACT = JSON.parse(
  readFileSync(
    resolve(process.cwd(), '../../apis/one_d4/src/test/resources/first_page_request.json'),
    'utf-8'
  )
);

afterEach(cleanup);

vi.mock('../api');
vi.mock('react-chessboard', () => ({
  Chessboard: ({ position }: { position: string }) => (
    <div data-testid="chessboard" data-fen={position} />
  ),
}));

const mockGame: GameRow = {
  gameUrl: 'https://chess.com/game/99',
  platform: 'chess.com',
  whiteUsername: '_prior',
  blackUsername: 'OpponentA',
  whiteElo: 1500,
  blackElo: 1480,
  timeClass: 'blitz',
  eco: 'A00',
  result: '1-0',
  playedAt: 1700000000,
  indexedAt: 1700001000,
  numMoves: 30,
  occurrences: { fork: [{ gameUrl: 'https://chess.com/game/99', motif: 'fork', moveNumber: 10, side: 'white', description: 'Fork' }] },
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

describe('GamesView', () => {
  beforeEach(() => {
    vi.mocked(api.query).mockResolvedValue({ games: [mockGame], count: 1 });
  });

  it('shows loading state then renders game rows', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    expect(screen.getByText('Loading…')).toBeInTheDocument();
    await waitFor(() => expect(screen.getByText('_prior')).toBeInTheDocument());
  });

  it('renders motif badges for games with motifs', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));
    expect(screen.getByText('fork')).toBeInTheDocument();
  });

  it('uses username filter when Search is clicked', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    const input = screen.getByPlaceholderText('e.g. Hikaru');
    fireEvent.change(input, { target: { value: '_prior' } });
    fireEvent.click(screen.getByRole('button', { name: 'Search' }));

    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({
          query: expect.stringContaining('_prior'),
        })
      )
    );
  });

  it('fetches with correct limit and offset on initial load', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    // Initial load: page 0, pageSize 25 → offset 0, limit 25
    expect(api.query).toHaveBeenCalledWith(
      expect.objectContaining({ limit: 25, offset: 0 })
    );
  });

  it('sends the exact request the backend first-page cache is keyed on', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    // The one_d4 API keeps this exact request warmed in memory (FirstPageCache.java) so the
    // first page load is served without a database round trip. Pinned against the shared
    // contract fixture, and on the FIRST call specifically: a matching request buried behind
    // some other first fetch would still pay the cold path on first paint.
    expect(vi.mocked(api.query).mock.calls[0][0]).toEqual(FIRST_PAGE_CONTRACT);
  });

  it('opens game detail panel when a row is clicked', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));
    const rows = screen.getAllByRole('row');
    fireEvent.click(rows[1]);
    expect(screen.getByText('_prior vs OpponentA')).toBeInTheDocument();
  });

  it('submits search on Enter key in the username input', async () => {
    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getByText('_prior'));

    const input = screen.getByPlaceholderText('e.g. Hikaru');
    fireEvent.change(input, { target: { value: 'Hikaru' } });
    fireEvent.keyDown(input, { key: 'Enter' });

    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({ query: expect.stringContaining('Hikaru') })
      )
    );
  });

  it('prefetches next page after initial load when page is full', async () => {
    // Return full page (25 games) to trigger hasMore=true and prefetch
    const fullPage = Array.from({ length: 25 }, (_, i) => ({
      ...mockGame,
      gameUrl: `https://chess.com/game/${i}`,
    }));
    vi.mocked(api.query).mockResolvedValue({ games: fullPage, count: 25 });

    render(<GamesView />, { wrapper: makeWrapper() });
    await waitFor(() => screen.getAllByRole('row').length > 1);

    // Prefetch fires a second query call for offset=25
    await waitFor(() =>
      expect(api.query).toHaveBeenCalledWith(
        expect.objectContaining({ limit: 25, offset: 25 })
      )
    );
  });
});
