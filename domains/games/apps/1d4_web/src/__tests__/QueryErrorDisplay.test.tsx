import React from 'react';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { describe, it, expect, vi, afterEach } from 'vitest';
import { QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router';
import QueryView from '../views/QueryView';
import { makeQueryClient } from '../api';

// The composition the unit tests cannot see: QueryView.test.tsx mocks ../api (so the unwrap
// never runs) and api.test.ts never renders (so the banner never shows). This suite runs the
// real fetch → api.request unwrap → react-query (with the real retry policy, via
// makeQueryClient) → error banner path, which is exactly what the reported bug broke: the
// banner used to show the raw {"error":…,"position":…} JSON, after ~7s of pointless retries.
vi.mock('react-chessboard', () => ({
  Chessboard: ({ position }: { position: string }) => (
    <div data-testid="chessboard" data-fen={position} />
  ),
}));

function Wrapper({ children }: { children: React.ReactNode }) {
  return (
    <QueryClientProvider client={makeQueryClient()}>
      <MemoryRouter>{children}</MemoryRouter>
    </QueryClientProvider>
  );
}

describe('QueryView error display through the real api layer', () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('shows the envelope error message, not the raw JSON, without retrying', async () => {
    const body =
      '{"error":"ChessQL has no NULL literal at position 12","position":12}';
    const fetchMock = vi.fn().mockResolvedValue({
      ok: false,
      status: 400,
      statusText: 'Bad Request',
      text: () => Promise.resolve(body),
    });
    vi.stubGlobal('fetch', fetchMock);

    render(<QueryView />, { wrapper: Wrapper });
    fireEvent.change(
      screen.getByPlaceholderText('e.g. motif(fork) AND white.elo >= 2500'),
      { target: { value: 'played.at = NULL' } }
    );
    fireEvent.click(screen.getByRole('button', { name: 'Run query' }));

    await waitFor(() =>
      expect(
        screen.getByText('ChessQL has no NULL literal at position 12')
      ).toBeInTheDocument()
    );
    expect(screen.queryByText(/\{"error"/)).not.toBeInTheDocument();
    // The 400 is deterministic; a single request proves the retry policy is actually wired.
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });
});
