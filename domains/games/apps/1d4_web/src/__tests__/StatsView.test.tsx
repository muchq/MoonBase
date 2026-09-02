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

function makeClient() {
  return new QueryClient({ defaultOptions: { queries: { retry: false } } });
}

function makeWrapper() {
  const qc = makeClient();
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
    // because nothing is filed under it.
    expect(table.getAllByRole('columnheader').map((h) => h.textContent)).toEqual([
      'Date', 'Entry', 'Requests', 'Web', 'MCP', 'API', 'Invalid', 'Failed', 'Snapshot',
    ]);
    const rows = table.getAllByRole('row').slice(1).map(cellsOf);
    // "40 of 50": the 3 invalid queries never reached the cache decision.
    expect(rows).toEqual([
      ['2026-09-01', 'aggregate', '1', '0', '0', '1', '0', '1', '—'],
      ['2026-09-01', 'query', '53', '50', '3', '0', '3', '0', '40 of 50'],
      ['2026-08-31', 'query', '5', '0', '0', '5', '0', '0', '0 of 5'],
    ]);
    // Every cell names its column for the stacked layout on a phone.
    for (const cell of table.getAllByRole('cell')) {
      expect(cell.getAttribute('data-label')).toBeTruthy();
    }
    expect(screen.getByText('Queries by day — last 30 days')).toBeInTheDocument();
  });

  it('shows the snapshot share only for queries that ran at all', async () => {
    vi.mocked(api.getQueryStats).mockResolvedValue({
      days: 7,
      rows: [
        // A day of nothing but rejected queries: no denominator, so no share.
        { date: '2026-09-02', entry: 'query', source: 'ui', outcome: 'invalid', cache: 'none', requests: 4 },
        // An aggregate never has a snapshot, whatever its rows claim.
        { date: '2026-09-02', entry: 'aggregate', source: 'ui', outcome: 'ok', cache: 'snapshot', requests: 2 },
      ],
    });
    render(<StatsView />, { wrapper: makeWrapper() });

    const table = within(await screen.findByTestId('queries-by-day'));
    const rows = table.getAllByRole('row').slice(1).map(cellsOf);
    expect(rows.map((r) => r[r.length - 1])).toEqual(['—', '—']);
    // The heading carries the window the server reported, not a constant.
    expect(screen.getByText('Queries by day — last 7 days')).toBeInTheDocument();
  });

  it('shows an Other column only when something is filed under it', async () => {
    vi.mocked(api.getQueryStats).mockResolvedValue({
      days: 30,
      rows: [
        ...stats.rows,
        { date: '2026-09-01', entry: 'query', source: 'cli', outcome: 'ok', cache: 'live', requests: 2 },
      ],
    });
    render(<StatsView />, { wrapper: makeWrapper() });

    const table = within(await screen.findByTestId('queries-by-day'));
    expect(table.getAllByRole('columnheader').map((h) => h.textContent)).toContain('Other');
    // A source this page does not know still adds up: total 55, Other 2, 42 of 52 answered.
    const queryRow = table.getAllByRole('row').slice(1).map(cellsOf)[1];
    expect(queryRow).toEqual(['2026-09-01', 'query', '55', '50', '3', '0', '2', '3', '0', '40 of 52']);
  });

  it('lists the busiest terms per kind, order-by apart from motifs', async () => {
    render(<StatsView />, { wrapper: makeWrapper() });

    const fields = within(await screen.findByTestId('terms-field'));
    expect(fields.getAllByRole('row').map(cellsOf)).toEqual([
      ['white.elo', '34'],
      ['eco', '12'],
    ]);
    expect(within(screen.getByTestId('terms-motif')).getAllByRole('row').map(cellsOf)).toEqual([['fork', '9']]);
    expect(within(screen.getByTestId('terms-order_by')).getAllByRole('row').map(cellsOf)).toEqual([['fork', '2']]);
    expect(within(screen.getByTestId('terms-group_by')).getAllByRole('row').map(cellsOf)).toEqual([
      ['opening_family', '7'],
    ]);
  });

  it('cuts each kind to the busiest fifteen', async () => {
    vi.mocked(api.getQueryTerms).mockResolvedValue({
      days: 30,
      rows: Array.from({ length: 20 }, (_, i) => ({
        entry: 'query',
        kind: 'motif',
        term: `motif_${i}`,
        requests: 100 - i,
      })),
    });
    render(<StatsView />, { wrapper: makeWrapper() });

    const motifs = within(await screen.findByTestId('terms-motif'));
    const rows = motifs.getAllByRole('row').map(cellsOf);
    expect(rows).toHaveLength(15);
    expect(rows[0]).toEqual(['motif_0', '100']);
    expect(rows[14]).toEqual(['motif_14', '86']);
  });

  it('says so when there is nothing yet', async () => {
    vi.mocked(api.getQueryStats).mockResolvedValue({ days: 30, rows: [] });
    vi.mocked(api.getQueryTerms).mockResolvedValue({ days: 30, rows: [] });
    render(<StatsView />, { wrapper: makeWrapper() });

    expect(await screen.findByText('No queries aggregated yet.')).toBeInTheDocument();
    expect(screen.getAllByText('none yet')).toHaveLength(4);
  });

  it('fails each panel on its own', async () => {
    vi.mocked(api.getQueryTerms).mockRejectedValue(new Error('Bad Gateway'));
    const { unmount } = render(<StatsView />, { wrapper: makeWrapper() });
    // The day table still renders while term usage reports its failure.
    expect(await screen.findByTestId('queries-by-day')).toBeInTheDocument();
    expect(await screen.findByText('Term usage is not available right now.')).toBeInTheDocument();
    unmount();

    vi.mocked(api.getQueryStats).mockRejectedValue(new Error('Bad Gateway'));
    vi.mocked(api.getQueryTerms).mockResolvedValue(terms);
    render(<StatsView />, { wrapper: makeWrapper() });
    expect(await screen.findByText('Stats are not available right now.')).toBeInTheDocument();
    expect(screen.getByText('Bad Gateway')).toBeInTheDocument();
    expect(await screen.findByTestId('terms-field')).toBeInTheDocument();
  });

  it('is in the nav and routed at /stats', async () => {
    render(
      <QueryClientProvider client={makeClient()}>
        <MemoryRouter initialEntries={['/stats']}>
          <App />
        </MemoryRouter>
      </QueryClientProvider>
    );
    const link = screen.getByRole('link', { name: 'Stats' });
    expect(link.getAttribute('href')).toBe('/stats');
    expect(link.className).toContain('active');
    expect(screen.getByRole('link', { name: 'MCP' }).className).not.toContain('active');
    expect(await screen.findByText(/Queries by day/)).toBeInTheDocument();
  });
});
