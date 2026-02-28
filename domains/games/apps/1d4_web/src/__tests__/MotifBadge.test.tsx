import { render, screen } from '@testing-library/react';
import { describe, it, expect } from 'vitest';
import MotifBadge from '../components/MotifBadge';

describe('MotifBadge', () => {
  it('renders label with underscores replaced by spaces', () => {
    render(<MotifBadge label="discovered_attack" />);
    expect(screen.getByText('discovered attack')).toBeInTheDocument();
  });

  it('has no title attribute when no occurrences provided', () => {
    render(<MotifBadge label="fork" />);
    const badge = screen.getByText('fork');
    expect(badge).not.toHaveAttribute('title');
  });

  it('builds tooltip text from a single occurrence', () => {
    const occs = [
      {
        gameUrl: 'https://chess.com/game/1',
        motif: 'fork',
        moveNumber: 12,
        side: 'white' as const,
        description: 'Knight forks king and queen',
      },
    ];
    render(<MotifBadge label="fork" occurrences={occs} />);
    const badge = screen.getByText('fork');
    expect(badge).toHaveAttribute(
      'title',
      'Move 12 (white): Knight forks king and queen'
    );
  });

  it('joins multiple occurrences with newlines in tooltip', () => {
    const occs = [
      { gameUrl: 'https://chess.com/game/1', motif: 'pin', moveNumber: 5, side: 'white' as const, description: 'Pin on d-file' },
      { gameUrl: 'https://chess.com/game/1', motif: 'pin', moveNumber: 20, side: 'black' as const, description: 'Absolute pin' },
    ];
    render(<MotifBadge label="pin" occurrences={occs} />);
    const badge = screen.getByText('pin');
    expect(badge).toHaveAttribute(
      'title',
      'Move 5 (white): Pin on d-file\nMove 20 (black): Absolute pin'
    );
  });
});
