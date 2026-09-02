import React from 'react';
import { cleanup, render, screen, waitFor, within, fireEvent } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import type { UserEvent } from '@testing-library/user-event';
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router';
import IndexView from '../views/IndexView';
import * as api from '../api';
import type { IndexRequest } from '../types';

vi.mock('../api');

/**
 * The form's month bounds are relative to "now", so the clock is pinned. Only
 * Date is faked — react-query and user-event keep real timers.
 */
const TODAY = new Date(2024, 5, 15);
const THIS_MONTH = '2024-06';

const completedRequest: IndexRequest = {
  id: 'req-1',
  player: 'hikaru',
  platform: 'CHESS_COM',
  startMonth: '2024-01',
  endMonth: '2024-03',
  status: 'COMPLETED',
  gamesIndexed: 42,
  errorMessage: null,
  excludeBullet: true,
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

const monthTrigger = (field: 'Start' | 'End') =>
  screen.getByRole('button', { name: new RegExp(`^${field} month`) });

/** Open a month picker and choose `month` in `year`, stepping the year as needed. */
async function pickMonth(
  user: UserEvent,
  field: 'Start' | 'End',
  year: number,
  month: string
) {
  await user.click(monthTrigger(field));
  const shownYear = () =>
    Number(within(screen.getByRole('dialog')).getByText(/^\d{4}$/).textContent);
  while (shownYear() > year) {
    await user.click(screen.getByRole('button', { name: 'Previous year' }));
  }
  while (shownYear() < year) {
    await user.click(screen.getByRole('button', { name: 'Next year' }));
  }
  await user.click(
    within(screen.getByRole('dialog')).getByRole('button', { name: month })
  );
}

function submitForm() {
  // fireEvent.click on a submit button doesn't propagate to onSubmit in jsdom;
  // fire the submit event on the form itself instead.
  fireEvent.submit(
    screen.getByLabelText('Username').closest('form') as HTMLFormElement
  );
}

describe('IndexView', () => {
  beforeEach(() => {
    vi.useFakeTimers({ toFake: ['Date'] });
    vi.setSystemTime(TODAY);
    vi.mocked(api.listIndexRequests).mockResolvedValue([completedRequest]);
    vi.mocked(api.createIndex).mockResolvedValue(completedRequest);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  function setup() {
    const user = userEvent.setup({ delay: null });
    render(<IndexView />, { wrapper: makeWrapper() });
    return user;
  }

  it('renders the status table with existing requests', async () => {
    setup();
    await waitFor(() => expect(screen.getByText('hikaru')).toBeInTheDocument());
    expect(screen.getByText('42')).toBeInTheDocument();
    // Not "2024-01 – 2024-03": the raw form wrapped to three lines on a phone.
    expect(screen.getByText('Jan – Mar 2024')).toBeInTheDocument();
  });

  it('collapses a single-month range and keeps both years across a boundary', async () => {
    vi.mocked(api.listIndexRequests).mockResolvedValue([
      { ...completedRequest, id: 'a', startMonth: '2024-03', endMonth: '2024-03' },
      { ...completedRequest, id: 'b', startMonth: '2025-11', endMonth: '2026-02' },
    ]);
    setup();

    await waitFor(() => expect(screen.getByText('Mar 2024')).toBeInTheDocument());
    expect(screen.getByText('Nov 2025 – Feb 2026')).toBeInTheDocument();
  });

  it('says whether each request\'s data survived retention', async () => {
    vi.mocked(api.listIndexRequests).mockResolvedValue([
      {
        ...completedRequest,
        id: 'live',
        player: 'live',
        data: {
          status: 'AVAILABLE',
          monthsAvailable: 3,
          monthsTotal: 3,
          expiresAt: Math.floor(TODAY.getTime() / 1000) + 3 * 86400,
        },
      },
      {
        ...completedRequest,
        id: 'gone',
        player: 'gone',
        data: {
          status: 'EXPIRED',
          monthsAvailable: 0,
          monthsTotal: 3,
          // Omitted, matching the wire: the API drops nulls.
        },
      },
      // Older API, or a request that hasn't completed: no signal to show.
      { ...completedRequest, id: 'silent', player: 'silent', status: 'PENDING' },
    ]);
    setup();

    await waitFor(() => expect(screen.getByRole('table')).toBeInTheDocument());
    // Scoped to the table: the explanatory note below it also says "Pruned".
    let table = within(screen.getByRole('table'));
    expect(table.getByText('Indexed')).toBeInTheDocument();
    expect(table.getByText('3d left')).toBeInTheDocument();
    // The pruned request is hidden until asked for; the count says how many.
    expect(table.queryByText('Pruned')).not.toBeInTheDocument();
    expect(screen.queryByText('gone')).not.toBeInTheDocument();
    const toggle = screen.getByRole('button', { name: 'Show 1 pruned' });
    expect(toggle).toHaveAttribute('aria-pressed', 'false');
    fireEvent.click(toggle);
    table = within(screen.getByRole('table'));
    expect(table.getByText('Pruned')).toBeInTheDocument();
    expect(screen.getByText('gone')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Hide pruned' })).toHaveAttribute('aria-pressed', 'true');
    fireEvent.click(screen.getByRole('button', { name: 'Hide pruned' }));
    expect(screen.queryByText('gone')).not.toBeInTheDocument();

    const silentRow = screen.getByText('silent').closest('tr') as HTMLTableRowElement;
    // Data column is 5th; Error is 6th and also renders a dash, so index precisely.
    expect(silentRow.cells[4]).toHaveTextContent('—');
    expect(silentRow.cells[4].querySelector('.data-badge')).toBeNull();
  });

  it('offers no toggle when nothing is pruned, and says so when everything is', async () => {
    setup();
    await waitFor(() => expect(screen.getByRole('table')).toBeInTheDocument());
    expect(screen.queryByRole('button', { name: /pruned/ })).not.toBeInTheDocument();

    cleanup();
    vi.mocked(api.listIndexRequests).mockResolvedValue([
      {
        ...completedRequest,
        id: 'gone',
        player: 'gone',
        data: { status: 'EXPIRED', monthsAvailable: 0, monthsTotal: 3 },
      },
    ]);
    setup();
    await waitFor(() =>
      expect(screen.getByText('Every recent request has been pruned.')).toBeInTheDocument()
    );
    expect(screen.queryByRole('table')).not.toBeInTheDocument();
    // The note still explains what pruned means, since the toggle is the only trace.
    expect(screen.getByText(/kept for 7 days/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'Show 1 pruned' }));
    expect(screen.getByRole('table')).toBeInTheDocument();
  });

  it('explains both retention windows so a pruned row is not a mystery', async () => {
    setup();
    await waitFor(() =>
      expect(screen.getByText(/kept for 7 days/)).toBeInTheDocument()
    );
    // The second window matters as much as the first: without it the note promises the row
    // stays "after that" indefinitely, and a user who comes back in a month finds it gone.
    expect(screen.getByText(/removed after 30 days/)).toBeInTheDocument();
  });

  it('lets a wide status table scroll instead of overflowing the panel', async () => {
    setup();
    await waitFor(() => expect(screen.getByRole('table')).toBeInTheDocument());
    // Without this wrapper the Error column ran off the right edge on a phone.
    expect(screen.getByRole('table').closest('.table-wrap')).not.toBeNull();
  });

  it('labels every cell so the narrow-screen card layout can name it', async () => {
    setup();
    await waitFor(() => expect(screen.getByText('hikaru')).toBeInTheDocument());

    const row = screen.getByText('hikaru').closest('tr') as HTMLTableRowElement;
    // Under 640px the header row is hidden and each cell renders data-label
    // via ::before, so a missing label silently drops a field's name on mobile.
    expect(Array.from(row.cells).map((c) => c.dataset.label)).toEqual([
      'Player',
      'Months',
      'Status',
      'Games',
      'Data',
      'Error',
    ]);
    // Headers and labels have to agree, or the card view contradicts the table.
    expect(
      Array.from(screen.getByRole('table').querySelectorAll('thead th')).map(
        (th) => th.textContent
      )
    ).toEqual(['Player', 'Months', 'Status', 'Games', 'Data', 'Error']);
  });

  it('marks an empty error cell so the card layout can drop the line', async () => {
    vi.mocked(api.listIndexRequests).mockResolvedValue([
      { ...completedRequest, id: 'ok', player: 'ok', errorMessage: null },
      { ...completedRequest, id: 'bad', player: 'bad', errorMessage: 'boom' },
    ]);
    setup();
    await waitFor(() => expect(screen.getByText('boom')).toBeInTheDocument());

    const okRow = screen.getByText('ok').closest('tr') as HTMLTableRowElement;
    const badRow = screen.getByText('bad').closest('tr') as HTMLTableRowElement;
    expect(okRow.cells[5]).toHaveClass('is-empty');
    expect(badRow.cells[5]).not.toHaveClass('is-empty');
  });

  it('shows empty state when no requests', async () => {
    vi.mocked(api.listIndexRequests).mockResolvedValue([]);
    setup();
    await waitFor(() =>
      expect(screen.getByText(/No recent requests/)).toBeInTheDocument()
    );
  });

  it('defaults both months to the current month', async () => {
    const user = setup();
    expect(monthTrigger('Start')).toHaveTextContent('Jun 2024');
    expect(monthTrigger('End')).toHaveTextContent('Jun 2024');

    await user.type(screen.getByLabelText('Username'), 'hikaru');
    submitForm();

    await waitFor(() =>
      expect(api.createIndex).toHaveBeenCalledWith({
        player: 'hikaru',
        platform: 'CHESS_COM',
        startMonth: THIS_MONTH,
        endMonth: THIS_MONTH,
        excludeBullet: true,
      })
    );
  });

  it('sends the months chosen in the pickers', async () => {
    const user = setup();

    await user.type(screen.getByLabelText('Username'), 'hikaru');
    await pickMonth(user, 'Start', 2024, 'Jan');
    await pickMonth(user, 'End', 2024, 'Mar');
    submitForm();

    await waitFor(() =>
      expect(api.createIndex).toHaveBeenCalledWith({
        player: 'hikaru',
        platform: 'CHESS_COM',
        startMonth: '2024-01',
        endMonth: '2024-03',
        excludeBullet: true,
      })
    );
  });

  it('carries the end month along when the start is pushed past it', async () => {
    const user = setup();

    await pickMonth(user, 'Start', 2024, 'Jan');
    await pickMonth(user, 'End', 2024, 'Feb');
    expect(monthTrigger('End')).toHaveTextContent('Feb 2024');

    await pickMonth(user, 'Start', 2024, 'May');
    expect(monthTrigger('End')).toHaveTextContent('May 2024');

    await user.type(screen.getByLabelText('Username'), 'hikaru');
    submitForm();

    await waitFor(() =>
      expect(api.createIndex).toHaveBeenCalledWith(
        expect.objectContaining({ startMonth: '2024-05', endMonth: '2024-05' })
      )
    );
  });

  it('will not offer an end month before the start, or a future month', async () => {
    const user = setup();

    await pickMonth(user, 'Start', 2024, 'Mar');
    await user.click(monthTrigger('End'));

    const dialog = within(screen.getByRole('dialog'));
    expect(dialog.getByRole('button', { name: 'Feb' })).toBeDisabled();
    expect(dialog.getByRole('button', { name: 'Mar' })).toBeEnabled();
    expect(dialog.getByRole('button', { name: 'Jun' })).toBeEnabled();
    // Today is June 2024, so July onward hasn't happened yet.
    expect(dialog.getByRole('button', { name: 'Jul' })).toBeDisabled();
  });

  it('sends excludeBullet: false when checkbox is unchecked', async () => {
    const user = setup();

    await user.type(screen.getByLabelText('Username'), 'hikaru');
    await user.click(screen.getByLabelText('Exclude bullet games'));
    submitForm();

    await waitFor(() =>
      expect(api.createIndex).toHaveBeenCalledWith(
        expect.objectContaining({ excludeBullet: false })
      )
    );
  });

  it('shows success message after successful submission', async () => {
    const user = setup();
    await user.type(screen.getByLabelText('Username'), 'hikaru');
    submitForm();

    await waitFor(() =>
      expect(screen.getByText('Request created.')).toBeInTheDocument()
    );
  });

  it('shows validation error when the username is missing', async () => {
    setup();
    submitForm();
    await waitFor(() =>
      expect(screen.getByText('Please enter a username.')).toBeInTheDocument()
    );
    expect(api.createIndex).not.toHaveBeenCalled();
  });
});
