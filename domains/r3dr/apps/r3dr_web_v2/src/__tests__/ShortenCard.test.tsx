import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import ShortenCard, { normalizeUrl, validateUrl } from '../components/ShortenCard';
import * as api from '../api';

vi.mock('../api', { spy: true });

const NOW = 1755000000000;
const DAY = 24 * 60 * 60 * 1000;

describe('normalizeUrl', () => {
  it('prefixes bare domains with https://', () => {
    expect(normalizeUrl('example.com/x')).toBe('https://example.com/x');
    expect(normalizeUrl('  example.com  ')).toBe('https://example.com');
  });

  it('leaves schemes alone, right or wrong', () => {
    expect(normalizeUrl('http://example.com')).toBe('http://example.com');
    expect(normalizeUrl('ftp://example.com')).toBe('ftp://example.com');
  });
});

describe('validateUrl', () => {
  it('mirrors the API traits', () => {
    expect(validateUrl('')).toMatch(/paste/i);
    expect(validateUrl('ftp://example.com')).toMatch(/http/);
    expect(validateUrl('http://g.c')).toMatch(/short/);
    expect(validateUrl(`https://example.com/${'a'.repeat(1000)}`)).toMatch(/1000/);
    expect(validateUrl('http://g.co')).toBeNull();
  });
});

describe('ShortenCard', () => {
  beforeEach(() => {
    vi.useFakeTimers({ toFake: ['Date'] });
    vi.setSystemTime(NOW);
  });

  it('mints with the default 7-day expiry and shows the short link', async () => {
    vi.mocked(api.shorten).mockResolvedValue({ slug: 'AQA' });
    const onMinted = vi.fn();
    render(<ShortenCard onMinted={onMinted} />);

    const user = userEvent.setup();
    await user.type(screen.getByLabelText('Long link'), 'https://example.com/page');
    await user.click(screen.getByRole('button', { name: 'Shorten' }));

    expect(api.shorten).toHaveBeenCalledWith('https://example.com/page', NOW + 7 * DAY);
    expect(await screen.findByRole('link', { name: 'r3dr.net/r/AQA' })).toHaveAttribute(
      'href',
      'https://r3dr.net/r/AQA'
    );
    expect(onMinted).toHaveBeenCalledWith({
      slug: 'AQA',
      longUrl: 'https://example.com/page',
      expiresAt: NOW + 7 * DAY,
    });
    // Ready for the next paste.
    expect(screen.getByLabelText('Long link')).toHaveValue('');
  });

  it('mints with a chosen expiry chip', async () => {
    vi.mocked(api.shorten).mockResolvedValue({ slug: 'DAA' });
    render(<ShortenCard onMinted={vi.fn()} />);

    const user = userEvent.setup();
    await user.click(screen.getByRole('radio', { name: '1 hour' }));
    expect(screen.getByRole('radio', { name: '1 hour' })).toHaveAttribute('aria-checked', 'true');

    await user.type(screen.getByLabelText('Long link'), 'https://example.com/page');
    await user.click(screen.getByRole('button', { name: 'Shorten' }));

    expect(api.shorten).toHaveBeenCalledWith('https://example.com/page', NOW + 60 * 60 * 1000);
  });

  it('blocks invalid input before it reaches the API', async () => {
    render(<ShortenCard onMinted={vi.fn()} />);

    const user = userEvent.setup();
    await user.type(screen.getByLabelText('Long link'), 'ftp://example.com');
    await user.click(screen.getByRole('button', { name: 'Shorten' }));

    expect(screen.getByRole('alert')).toHaveTextContent('http:// or https://');
    expect(api.shorten).not.toHaveBeenCalled();
  });

  it('shows the API error and clears it on the next success', async () => {
    vi.mocked(api.shorten).mockRejectedValueOnce(new Error('expiresAt is in the past'));
    vi.mocked(api.shorten).mockResolvedValueOnce({ slug: 'AQA' });
    render(<ShortenCard onMinted={vi.fn()} />);

    const user = userEvent.setup();
    await user.type(screen.getByLabelText('Long link'), 'https://example.com/page');
    await user.click(screen.getByRole('button', { name: 'Shorten' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('expiresAt is in the past');

    await user.type(screen.getByLabelText('Long link'), 'https://example.com/page');
    await user.click(screen.getByRole('button', { name: 'Shorten' }));
    expect(await screen.findByRole('link', { name: 'r3dr.net/r/AQA' })).toBeInTheDocument();
    expect(screen.queryByRole('alert')).not.toBeInTheDocument();
  });
});
