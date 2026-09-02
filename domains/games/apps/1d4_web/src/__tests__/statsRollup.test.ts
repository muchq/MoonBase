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
      { date: '2026-09-01', entry: 'aggregate', total: 1, bySource: { api: 1 }, invalid: 0, failed: 1, snapshot: 0, live: 0 },
      { date: '2026-09-01', entry: 'query', total: 53, bySource: { ui: 50, mcp: 3 }, invalid: 3, failed: 0, snapshot: 40, live: 10 },
      { date: '2026-08-31', entry: 'query', total: 5, bySource: { api: 5 }, invalid: 0, failed: 0, snapshot: 0, live: 5 },
    ]);
  });

  it('notices an unknown source without hiding it', () => {
    expect(hasOtherSource(rows)).toBe(false);
    expect(hasOtherSource([...rows, { ...rows[0], source: 'other' }])).toBe(true);
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

  it('sums a kind across entries, busiest first with a stable tie order', () => {
    expect(topTerms(terms, 'field', 10)).toEqual([
      { term: 'white.elo', requests: 34 },
      { term: 'eco', requests: 12 },
    ]);
    expect(topTerms(terms, 'motif', 10)).toEqual([
      { term: 'fork', requests: 11 },
      { term: 'pin', requests: 9 },
    ]);
    expect(topTerms(terms, 'group_by', 10)).toEqual([{ term: 'opening_family', requests: 7 }]);
  });

  it('honours the limit and knows nothing of order_by as its own kind', () => {
    expect(topTerms(terms, 'field', 1)).toEqual([{ term: 'white.elo', requests: 34 }]);
    expect(topTerms(terms, 'order_by', 10)).toEqual([]);
  });
});
