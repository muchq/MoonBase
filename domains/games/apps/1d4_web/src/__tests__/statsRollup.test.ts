import { describe, it, expect } from 'vitest';
import { hasOtherSource, rollupDays, topTerms } from '../statsRollup';

const rows = [
  { date: '2026-09-01', entry: 'query', source: 'ui', outcome: 'ok', cache: 'snapshot', requests: 40 },
  { date: '2026-09-01', entry: 'query', source: 'ui', outcome: 'ok', cache: 'live', requests: 10 },
  { date: '2026-09-01', entry: 'query', source: 'mcp', outcome: 'invalid', cache: 'none', requests: 3 },
  { date: '2026-09-01', entry: 'aggregate', source: 'api', outcome: 'failed', cache: 'none', requests: 1 },
  { date: '2026-08-31', entry: 'query', source: 'api', outcome: 'ok', cache: 'live', requests: 5 },
];

describe('rollupDays', () => {
  it('sums per day and entry, newest first, with the three splits', () => {
    expect(rollupDays(rows)).toEqual([
      { date: '2026-09-01', entry: 'aggregate', total: 1, bySource: { api: 1 }, invalid: 0, failed: 1, snapshot: 0, answered: 0 },
      { date: '2026-09-01', entry: 'query', total: 53, bySource: { ui: 50, mcp: 3 }, invalid: 3, failed: 0, snapshot: 40, answered: 50 },
      { date: '2026-08-31', entry: 'query', total: 5, bySource: { api: 5 }, invalid: 0, failed: 0, snapshot: 0, answered: 5 },
    ]);
  });

  it('files a source it does not know under other, so the splits still sum', () => {
    const days = rollupDays([...rows, { ...rows[0], source: 'cli', requests: 2 }]);
    expect(days[1].bySource).toEqual({ ui: 50, mcp: 3, other: 2 });
    expect(days[1].total).toBe(55);
    expect(hasOtherSource(days)).toBe(true);
    expect(hasOtherSource(rollupDays(rows))).toBe(false);
  });

  it('counts a cache word it does not know as answered, not as never run', () => {
    const days = rollupDays([{ ...rows[0], cache: 'other', requests: 2 }]);
    expect(days[0].answered).toBe(2);
    expect(days[0].snapshot).toBe(0);
  });
});

describe('topTerms', () => {
  const terms = [
    { entry: 'query', kind: 'field', term: 'white.elo', requests: 30 },
    { entry: 'aggregate', kind: 'field', term: 'white.elo', requests: 4 },
    { entry: 'query', kind: 'field', term: 'eco', requests: 12 },
    { entry: 'query', kind: 'motif', term: 'fork', requests: 9 },
    { entry: 'query', kind: 'order_by', term: 'fork', requests: 2 },
    { entry: 'query', kind: 'motif', term: 'pin', requests: 9 },
    { entry: 'aggregate', kind: 'group_by', term: 'opening_family', requests: 7 },
  ];

  it('sums a kind across entries, busiest first, ties by name', () => {
    expect(topTerms(terms, 'field', 10)).toEqual([
      { term: 'white.elo', requests: 34 },
      { term: 'eco', requests: 12 },
    ]);
    // fork and pin tie at 9; fork sorts first by name, and the order-by fork
    // is its own kind rather than two more on the motif.
    expect(topTerms(terms, 'motif', 10)).toEqual([
      { term: 'fork', requests: 9 },
      { term: 'pin', requests: 9 },
    ]);
    expect(topTerms(terms, 'order_by', 10)).toEqual([{ term: 'fork', requests: 2 }]);
    expect(topTerms(terms, 'group_by', 10)).toEqual([{ term: 'opening_family', requests: 7 }]);
  });

  it('honours the limit', () => {
    expect(topTerms(terms, 'field', 1)).toEqual([{ term: 'white.elo', requests: 34 }]);
  });
});
