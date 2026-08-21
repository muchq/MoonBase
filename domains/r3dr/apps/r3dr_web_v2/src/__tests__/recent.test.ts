import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { addRecent, clearRecent, loadRecent, type RecentLink } from '../recent';

const NOW = 1755000000000;

const link = (slug: string, expiresAt = NOW + 1000): RecentLink => ({
  slug,
  longUrl: `https://example.com/${slug}`,
  expiresAt,
});

beforeEach(() => localStorage.clear());
afterEach(() => vi.restoreAllMocks());

describe('addRecent + loadRecent', () => {
  it('round-trips through localStorage newest first', () => {
    const one = addRecent([], link('AAA'));
    addRecent(one, link('BBB'));

    const loaded = loadRecent(NOW);
    expect(loaded.map((l) => l.slug)).toEqual(['BBB', 'AAA']);
  });

  it('dedupes by slug and caps at five', () => {
    let links: RecentLink[] = [];
    for (const slug of ['aaa', 'bbb', 'ccc', 'ddd', 'eee', 'fff']) {
      links = addRecent(links, link(slug));
    }
    links = addRecent(links, link('ddd'));

    expect(links.map((l) => l.slug)).toEqual(['ddd', 'fff', 'eee', 'ccc', 'bbb']);
    expect(loadRecent(NOW).map((l) => l.slug)).toEqual(['ddd', 'fff', 'eee', 'ccc', 'bbb']);
  });

  it('drops expired links on load', () => {
    addRecent(addRecent([], link('dead', NOW - 1)), link('live', NOW + 1));

    expect(loadRecent(NOW).map((l) => l.slug)).toEqual(['live']);
  });

  it('shrugs off garbage and missing storage', () => {
    localStorage.setItem('r3dr.recent', 'not json');
    expect(loadRecent(NOW)).toEqual([]);

    localStorage.setItem('r3dr.recent', '{"an":"object"}');
    expect(loadRecent(NOW)).toEqual([]);

    localStorage.setItem('r3dr.recent', '[{"slug":1}]');
    expect(loadRecent(NOW)).toEqual([]);
  });

  it('drops entries whose slug is not slug-shaped', () => {
    localStorage.setItem(
      'r3dr.recent',
      JSON.stringify([
        { slug: '<img src=x>', longUrl: 'https://example.com/a', expiresAt: NOW + 1000 },
        { slug: 'javascript:alert(1)', longUrl: 'https://example.com/b', expiresAt: NOW + 1000 },
        { slug: 'AQA', longUrl: 'https://example.com/c', expiresAt: NOW + 1000 },
      ])
    );
    expect(loadRecent(NOW).map((l) => l.slug)).toEqual(['AQA']);
  });

  it('still returns the list when localStorage throws', () => {
    // Restored in afterEach so a failed expect can't leak the throwing spy.
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('quota');
    });
    expect(addRecent([], link('AAA')).map((l) => l.slug)).toEqual(['AAA']);
  });
});

describe('clearRecent', () => {
  it('empties storage', () => {
    addRecent([], link('AAA'));
    clearRecent();
    expect(loadRecent(NOW)).toEqual([]);
  });
});
