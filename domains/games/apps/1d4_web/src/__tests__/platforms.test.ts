import { describe, it, expect } from 'vitest';
import { PLATFORMS, platformLabel } from '../platforms';

describe('platformLabel', () => {
  it('spells a stored platform the way its site does', () => {
    expect(platformLabel('CHESS_COM')).toBe('chess.com');
    expect(platformLabel('LICHESS')).toBe('lichess');
  });

  // A row this build does not recognize still came from somewhere. Blanking it
  // would say it came from nowhere, which is worse than an ugly string.
  it('shows an unknown platform as stored rather than blank', () => {
    expect(platformLabel('CHESS24_COM')).toBe('CHESS24_COM');
    expect(platformLabel('')).toBe('');
  });

  // The API canonicalizes before storing, so these are the only spellings that
  // reach the client — a label keyed on anything else would never be found.
  it('keys on the stored spelling, not a friendly one', () => {
    expect(platformLabel('chess.com')).toBe('chess.com');
    expect(platformLabel('lichess')).toBe('lichess');
    expect(PLATFORMS.map((p) => p.value)).toEqual(['CHESS_COM', 'LICHESS']);
  });
});
