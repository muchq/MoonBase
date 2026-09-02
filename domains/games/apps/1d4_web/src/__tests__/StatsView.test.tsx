import React from 'react';
import { render, screen, within } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router';
import App from '../App';
import StatsView from '../views/StatsView';
import * as api from '../api';

vi.mock('../api');

const stats = {
  days: 30,
  rows: [
    { date: '2026-09-01', entry: 'query', source: 'ui', outcome: 'ok', cache: 'snapshot', requests: 40 },
    { date: '2026-09-01', entry: 'query', source: 'ui', outcome: 'ok', cache: 'live', requests: 10 },
    { date: '2026-09-01', entry: 'query', source: 'mcp', outcome: 'invalid', cache: 'none', requests: 3 },
    { date: '2026-09-01', entry: 'aggregate', source: 'api', outcome: 'failed', cache: 'none', requests: 1 },
    { date: '2026-08-31', entry: 'query', source: 'api', outcome: 'ok', cache: 'live', requests: 5 },
  ],
};

const terms = {
  days: 30,
  rows: [
    { entry: 'query', kind: 'field', term: 'white.elo', requests: 30 },
    { entry: 'aggregate', kind: 'field', term: 'white.elo', requests: 4 },
    { entry: 'query', kind: 'field', term: 'eco', requests: 12 },
    { entry: 'query', kind: 'motif', term: 'fork', requests: 9 },
    { entry: 'query', kind: 'order_by', term: 'fork', requests: 2 },
    { entry: 'aggregate', kind: 'group_by', term: 'opening_family', requests: 7 },
  ],
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

const cellsOf = (row: HTMLElement) =>
  within(row)
    .getAllByRole('cell')
    .map((c) => c.textContent);

describe('StatsView', () => {
  beforeEach(() => {
    vi.mocked(api.getQueryStats).mockResolvedValue(stats);
    vi.mocked(api.getQueryTerms).mockResolvedValue(terms);
  });

  it('rolls queries up per day and entry with the source, error, and snapshot splits', async () => {
    render(<StatsView />, { wrapper: makeWrapper() });

    const table = within(await screen.findByTestId('queries-by-day'));
    // Newest first; query before aggregate within a day; no "Other" column
    // because no row carries an unknown source.
    expect(table.getAllByRole('columnheader').map((h) => h.textContent)).toEqual([
      'Date', 'Entry', 'Requests', 'Web', 'MCP', 'API', 'Invalid', 'Failed', 'Snapshot',
    ]);
    const rows = table.getAllByRole('row').slice(1).map(cellsOf);
    expect(rows).toEqual([
      ['2026-09-01', 'aggregate', '1', '0', '0', '1', '0', '1', '—'],
      ['2026-09-01', 'query', '53', '50', '3', '0', '3', '0', '40 of 50'],
      ['2026-08-31', 'query', '5', '0', '0', '5', '0', '0', '0 of 5'],
    ]);
  });

  it('shows an Other column only when the aggregator filed something under it', async () => {
    vi.mocked(api.getQueryStats).mockResolvedValue({
      days: 30,
      rows: [
        ...stats.rows,
        { date: '2026-09-01', entry: 'query', source: 'other', outcome: 'ok', cache: 'live', requests: 2 },
      ],
    });
    render(<StatsView />, { wrapper: makeWrapper() });

    const table = within(await screen.findByTestId('queries-by-day'));
    expect(table.getAllByRole('columnheader').map((h) => h.textContent)).toContain('Other');
    const queryRow = table.getAllByRole('row').slice(1).map(cellsOf)[1];
    expect(queryRow).toEqual(['2026-09-01', 'query', '55', '50', '3', '0', '2', '3', '0', '40 of 52']);
  });

  it('lists the busiest terms per kind, folding order-by motifs into motifs', async () => {
    render(<StatsView />, { wrapper: makeWrapper() });

    const fields = within(await screen.findByTestId('terms-field'));
    expect(fields.getAllByRole('row').map(cellsOf)).toEqual([
      ['white.elo', '34'],
      ['eco', '12'],
    ]);
    const motifs = within(screen.getByTestId('terms-motif'));
    expect(motifs.getAllByRole('row').map(cellsOf)).toEqual([['fork', '11']]);
    const groupBy = within(screen.getByTestId('terms-group_by'));
    expect(groupBy.getAllByRole('row').map(cellsOf)).toEqual([['opening_family', '7']]);
  });

  it('says so when there is nothing yet, and when the service is down', async () => {
    vi.mocked(api.getQueryStats).mockResolvedValue({ days: 30, rows: [] });
    vi.mocked(api.getQueryTerms).mockResolvedValue({ days: 30, rows: [] });
    const { unmount } = render(<StatsView />, { wrapper: makeWrapper() });
    expect(await screen.findByText('No queries aggregated yet.')).toBeInTheDocument();
    expect(screen.getAllByText('none yet')).toHaveLength(3);
    unmount();

    vi.mocked(api.getQueryStats).mockRejectedValue(new Error('Bad Gateway'));
    vi.mocked(api.getQueryTerms).mockRejectedValue(new Error('Bad Gateway'));
    render(<StatsView />, { wrapper: makeWrapper() });
    expect(await screen.findByText('Stats are not available right now.')).toBeInTheDocument();
    expect(screen.getByText('Bad Gateway')).toBeInTheDocument();
  });

  it('is in the nav and routed at /stats', async () => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    render(
      <QueryClientProvider client={qc}>
        <MemoryRouter initialEntries={['/stats']}>
          <App />
        </MemoryRouter>
      </QueryClientProvider>
    );
    expect(screen.getByRole('link', { name: 'Stats' }).getAttribute('href')).toBe('/stats');
    expect(await screen.findByText(/Queries by day/)).toBeInTheDocument();
  });
});
