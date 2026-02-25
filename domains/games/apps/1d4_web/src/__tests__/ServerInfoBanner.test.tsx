import React from 'react';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import ServerInfoBanner from '../components/ServerInfoBanner';
import * as api from '../api';

vi.mock('../api');

function makeWrapper() {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return function Wrapper({ children }: { children: React.ReactNode }) {
    return <QueryClientProvider client={qc}>{children}</QueryClientProvider>;
  };
}

describe('ServerInfoBanner', () => {
  beforeEach(() => {
    vi.mocked(api.getServerInfo).mockResolvedValue({ maintenanceWindows: [] });
  });

  it('renders nothing when maintenanceWindows is empty', async () => {
    vi.mocked(api.getServerInfo).mockResolvedValue({ maintenanceWindows: [] });

    render(<ServerInfoBanner />, { wrapper: makeWrapper() });

    await waitFor(() => expect(api.getServerInfo).toHaveBeenCalled());
    expect(screen.queryByRole('status')).not.toBeInTheDocument();
  });

  it('renders banner when maintenanceWindows has entries', async () => {
    vi.mocked(api.getServerInfo).mockResolvedValue({
      maintenanceWindows: [
        {
          message: 'Scheduled server restart tonight at midnight UTC.',
          scheduledAt: '2024-06-16T00:00:00Z',
        },
      ],
    });

    render(<ServerInfoBanner />, { wrapper: makeWrapper() });

    await waitFor(() =>
      expect(
        screen.getByText('Scheduled server restart tonight at midnight UTC.')
      ).toBeInTheDocument()
    );
    expect(screen.getByRole('status')).toBeInTheDocument();
  });

  it('renders all maintenance window messages', async () => {
    vi.mocked(api.getServerInfo).mockResolvedValue({
      maintenanceWindows: [
        { message: 'Window one', scheduledAt: '2024-06-16T00:00:00Z' },
        { message: 'Window two', scheduledAt: '2024-06-16T00:00:00Z' },
      ],
    });

    render(<ServerInfoBanner />, { wrapper: makeWrapper() });

    await waitFor(() =>
      expect(screen.getByText('Window one')).toBeInTheDocument()
    );
    expect(screen.getByText('Window two')).toBeInTheDocument();
  });

  it('dismisses banner when dismiss button is clicked', async () => {
    vi.mocked(api.getServerInfo).mockResolvedValue({
      maintenanceWindows: [
        {
          message: 'Restart tonight',
          scheduledAt: '2024-06-16T00:00:00Z',
        },
      ],
    });

    render(<ServerInfoBanner />, { wrapper: makeWrapper() });

    await waitFor(() =>
      expect(screen.getByText('Restart tonight')).toBeInTheDocument()
    );

    fireEvent.click(screen.getByRole('button', { name: 'Dismiss notification' }));

    expect(screen.queryByRole('status')).not.toBeInTheDocument();
  });

  it('does not render before data is fetched', () => {
    vi.mocked(api.getServerInfo).mockImplementation(
      () => new Promise(() => {}) // never resolves
    );

    render(<ServerInfoBanner />, { wrapper: makeWrapper() });

    expect(screen.queryByRole('status')).not.toBeInTheDocument();
  });
});
