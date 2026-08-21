import { render, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import App from '../App';
import * as api from '../api';

vi.mock('../api', { spy: true });

const NOW = 1755000000000;

describe('App', () => {
  beforeEach(() => {
    localStorage.clear();
    vi.useFakeTimers({ toFake: ['Date'] });
    vi.setSystemTime(NOW);
  });

  it('adds a minted link to Recent links and persists it', async () => {
    vi.mocked(api.shorten).mockResolvedValue({ slug: 'AQA' });
    render(<App />);

    const user = userEvent.setup();
    await user.type(screen.getByLabelText('Long link'), 'https://example.com/page');
    await user.click(screen.getByRole('button', { name: 'Shorten' }));

    const recent = await screen.findByRole('region', { name: 'Recent links' });
    expect(within(recent).getByRole('link', { name: 'iili.uk/r/AQA' })).toBeInTheDocument();
    expect(localStorage.getItem('iili.recent')).toContain('"AQA"');
  });

  it('boots with stored links, skipping expired ones', () => {
    localStorage.setItem(
      'iili.recent',
      JSON.stringify([
        { slug: 'AQA', longUrl: 'https://example.com/live', expiresAt: NOW + 1000 },
        { slug: 'DAA', longUrl: 'https://example.com/dead', expiresAt: NOW - 1000 },
      ])
    );
    render(<App />);

    const recent = screen.getByRole('region', { name: 'Recent links' });
    expect(within(recent).getByRole('link', { name: 'iili.uk/r/AQA' })).toBeInTheDocument();
    expect(within(recent).queryByText(/DAA/)).not.toBeInTheDocument();
  });

  it('clears the list and the storage together', async () => {
    localStorage.setItem(
      'iili.recent',
      JSON.stringify([{ slug: 'AQA', longUrl: 'https://example.com/x', expiresAt: NOW + 1000 }])
    );
    render(<App />);

    const user = userEvent.setup();
    await user.click(screen.getByRole('button', { name: 'Clear recent links' }));

    expect(screen.queryByRole('region', { name: 'Recent links' })).not.toBeInTheDocument();
    expect(localStorage.getItem('iili.recent')).toBeNull();
  });
});
