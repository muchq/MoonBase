import { render, screen } from '@testing-library/react';
import { describe, it, expect } from 'vitest';
import DataBadge, { formatCountdown } from '../components/DataBadge';
import type { DataAvailability } from '../types';

const NOW = Date.UTC(2026, 6, 30, 12, 0, 0);
const inHours = (h: number) => NOW / 1000 + h * 3600;

function availability(over: Partial<DataAvailability> = {}): DataAvailability {
  return {
    status: 'AVAILABLE',
    monthsAvailable: 1,
    monthsTotal: 1,
    expiresAt: inHours(72),
    ...over,
  };
}

describe('formatCountdown', () => {
  it('reports days beyond two, hours below, and never a negative', () => {
    expect(formatCountdown(inHours(72), NOW)).toBe('3d');
    expect(formatCountdown(inHours(49), NOW)).toBe('2d');
    expect(formatCountdown(inHours(47), NOW)).toBe('47h');
    expect(formatCountdown(inHours(1), NOW)).toBe('1h');
    expect(formatCountdown(inHours(0.5), NOW)).toBe('<1h');
    // The sweep runs hourly, so a lapsed deadline means "not yet collected",
    // not "-3h".
    expect(formatCountdown(inHours(-5), NOW)).toBe('soon');
    expect(formatCountdown(NOW / 1000, NOW)).toBe('soon');
  });
});

describe('DataBadge', () => {
  it('renders a dash when the request has no availability yet', () => {
    const { container } = render(<DataBadge data={null} now={NOW} />);
    expect(container).toHaveTextContent('—');
    expect(container.querySelector('.data-badge')).toBeNull();
  });

  it('renders a dash for an undefined field, as an older API would send', () => {
    const { container } = render(<DataBadge data={undefined} now={NOW} />);
    expect(container).toHaveTextContent('—');
  });

  it('shows the games are still indexed, with time remaining', () => {
    const { container } = render(<DataBadge data={availability()} now={NOW} />);

    expect(screen.getByText('Indexed')).toBeInTheDocument();
    expect(screen.getByText('3d left')).toBeInTheDocument();
    expect(container.querySelector('.data-badge')).toHaveClass('available');
  });

  it('says Pruned once retention has swept everything', () => {
    const { container } = render(
      <DataBadge
        data={availability({ status: 'EXPIRED', monthsAvailable: 0, expiresAt: null })}
        now={NOW}
      />
    );

    // The word the user is looking for when the row still reads "COMPLETED, 325 games".
    expect(screen.getByText('Pruned')).toBeInTheDocument();
    expect(container.querySelector('.data-badge')).toHaveClass('expired');
    expect(container).not.toHaveTextContent('left');
  });

  it('counts the surviving months when only some were swept', () => {
    const { container } = render(
      <DataBadge
        data={availability({ status: 'PARTIAL', monthsAvailable: 2, monthsTotal: 5 })}
        now={NOW}
      />
    );

    expect(screen.getByText(/2\/5 months/)).toBeInTheDocument();
    expect(screen.getByText('3d left')).toBeInTheDocument();
    expect(container.querySelector('.data-badge')).toHaveClass('partial');
  });

  it('renders UNKNOWN without inventing a status', () => {
    const { container } = render(
      <DataBadge data={availability({ status: 'UNKNOWN', monthsTotal: 0 })} now={NOW} />
    );

    expect(screen.getByText('Unknown')).toBeInTheDocument();
    expect(container.querySelector('.data-badge')).toHaveClass('unknown');
  });

  it('omits the countdown when no expiry is known', () => {
    const { container } = render(
      <DataBadge data={availability({ expiresAt: null })} now={NOW} />
    );

    expect(screen.getByText('Indexed')).toBeInTheDocument();
    expect(container).not.toHaveTextContent('left');
  });
});
