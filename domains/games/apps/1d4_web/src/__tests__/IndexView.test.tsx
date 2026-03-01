import React from 'react';
import { render, screen, waitFor, fireEvent } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { MemoryRouter } from 'react-router-dom';
import IndexView from '../views/IndexView';
import * as api from '../api';
import type { IndexRequest } from '../types';

vi.mock('../api');

const completedRequest: IndexRequest = {
  id: 'req-1',
  player: 'hikaru',
  platform: 'CHESS_COM',
  startMonth: '2024-01',
  endMonth: '2024-03',
  status: 'COMPLETED',
  gamesIndexed: 42,
  errorMessage: null,
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

describe('IndexView', () => {
  beforeEach(() => {
    vi.mocked(api.listIndexRequests).mockResolvedValue([completedRequest]);
    vi.mocked(api.createIndex).mockResolvedValue(completedRequest);
  });

  it('renders the status table with existing requests', async () => {
    render(<IndexView />, { wrapper: makeWrapper() });
    await waitFor(() => expect(screen.getByText('hikaru')).toBeInTheDocument());
    expect(screen.getByText('42')).toBeInTheDocument();
    expect(screen.getByText('2024-01 – 2024-03')).toBeInTheDocument();
  });

  it('shows empty state when no requests', async () => {
    vi.mocked(api.listIndexRequests).mockResolvedValue([]);
    render(<IndexView />, { wrapper: makeWrapper() });
    await waitFor(() =>
      expect(screen.getByText(/No recent requests/)).toBeInTheDocument()
    );
  });

  function submitForm() {
    // fireEvent.click on a submit button doesn't propagate to onSubmit in jsdom;
    // fire the submit event on the form itself instead.
    fireEvent.submit(
      screen.getByLabelText('Username').closest('form') as HTMLFormElement
    );
  }

  it('calls createIndex with correct payload on submit', async () => {
    render(<IndexView />, { wrapper: makeWrapper() });

    fireEvent.change(screen.getByLabelText('Username'), {
      target: { value: 'hikaru' },
    });
    fireEvent.change(screen.getByLabelText('Start month (YYYY-MM)'), {
      target: { value: '2024-01' },
    });
    fireEvent.change(screen.getByLabelText('End month (YYYY-MM)'), {
      target: { value: '2024-03' },
    });
    submitForm();

    await waitFor(() =>
      expect(api.createIndex).toHaveBeenCalledWith({
        player: 'hikaru',
        platform: 'CHESS_COM',
        startMonth: '2024-01',
        endMonth: '2024-03',
        includeBullet: false,
      })
    );
  });

  it('shows success message after successful submission', async () => {
    render(<IndexView />, { wrapper: makeWrapper() });
    fireEvent.change(screen.getByLabelText('Username'), {
      target: { value: 'hikaru' },
    });
    fireEvent.change(screen.getByLabelText('Start month (YYYY-MM)'), {
      target: { value: '2024-01' },
    });
    fireEvent.change(screen.getByLabelText('End month (YYYY-MM)'), {
      target: { value: '2024-03' },
    });
    submitForm();

    await waitFor(() =>
      expect(screen.getByText('Request created.')).toBeInTheDocument()
    );
  });

  it('shows validation error when form is submitted empty', async () => {
    render(<IndexView />, { wrapper: makeWrapper() });
    submitForm();
    await waitFor(() =>
      expect(
        screen.getByText(/Please fill in username and both months/)
      ).toBeInTheDocument()
    );
    expect(api.createIndex).not.toHaveBeenCalled();
  });
});
