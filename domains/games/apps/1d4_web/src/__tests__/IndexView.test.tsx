import React from 'react';
import { render, screen, waitFor, within, fireEvent } from '@testing-library/react';
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
    expect(screen.getByText('2024-01 – 2024-03')).toBeInTheDocument();
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
