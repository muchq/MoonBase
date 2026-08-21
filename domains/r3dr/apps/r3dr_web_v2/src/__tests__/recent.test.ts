import { beforeEach, describe, expect, it, vi } from 'vitest';
import { addRecent, clearRecent, loadRecent, type RecentLink } from '../recent';

const NOW = 1755000000000;

const link = (slug: string, expiresAt = NOW + 1000): RecentLink => ({
  slug,
  longUrl: `https://example.com/${slug}`,
  expiresAt,
});

beforeEach(() => localStorage.clear());

describe('addRecent + loadRecent', () => {
  it('round-trips through localStorage newest first', () => {
    const one = addRecent([], link('AAA'));
    addRecent(one, link('BBB'));

    const loaded = loadRecent(NOW);
    expect(loaded.map((l) => l.slug)).toEqual(['BBB', 'AAA']);
  });

  it('dedupes by slug and caps at five', () => {
    let links: RecentLink[] = [];
    for (const slug of ['a', 'b', 'c', 'd', 'e', 'f']) {
      links = addRecent(links, link(slug));
    }
    links = addRecent(links, link('d'));

    expect(links.map((l) => l.slug)).toEqual(['d', 'f', 'e', 'c', 'b']);
    expect(loadRecent(NOW).map((l) => l.slug)).toEqual(['d', 'f', 'e', 'c', 'b']);
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

  it('still returns the list when localStorage throws', () => {
    const setItem = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('quota');
    });
    expect(addRecent([], link('AAA')).map((l) => l.slug)).toEqual(['AAA']);
    setItem.mockRestore();
  });
});

describe('clearRecent', () => {
  it('empties storage', () => {
    addRecent([], link('AAA'));
    clearRecent();
    expect(loadRecent(NOW)).toEqual([]);
  });
});
