import type { QueryStatRow, QueryTermRow } from './types';

export const SOURCES = ['ui', 'mcp', 'api'] as const;

export const SOURCE_LABELS: Record<string, string> = {
  ui: 'Web',
  mcp: 'MCP',
  api: 'API',
  other: 'Other',
};

export const TERM_KINDS = ['field', 'motif', 'order_by', 'group_by'] as const;

export const KIND_LABELS: Record<string, string> = {
  field: 'Fields',
  motif: 'Motifs',
  order_by: 'Ordered by',
  group_by: 'Grouped by',
};

export interface DayRow {
  date: string;
  entry: string;
  total: number;
  bySource: Record<string, number>;
  invalid: number;
  failed: number;
  snapshot: number;
  /** Requests that reached the cache decision: served from the snapshot or run live. */
  answered: number;
}

// One row per day and entry point, newest first, with the source split, the
// error split, and (queries only) how many were answered from the first-page
// snapshot rather than run live. A source outside the three known ones — the
// aggregator files a word it does not know under "other", and this page
// treats any word it does not know the same way — lands in the Other column,
// so the splits always sum to the total and drift is visible, not hidden.
export function rollupDays(rows: QueryStatRow[]): DayRow[] {
  const days = new Map<string, DayRow>();
  for (const row of rows) {
    const key = `${row.date} ${row.entry}`;
    let day = days.get(key);
    if (!day) {
      day = {
        date: row.date,
        entry: row.entry,
        total: 0,
        bySource: {},
        invalid: 0,
        failed: 0,
        snapshot: 0,
        answered: 0,
      };
      days.set(key, day);
    }
    const source = (SOURCES as readonly string[]).includes(row.source) ? row.source : 'other';
    day.total += row.requests;
    day.bySource[source] = (day.bySource[source] ?? 0) + row.requests;
    if (row.outcome === 'invalid') day.invalid += row.requests;
    if (row.outcome === 'failed') day.failed += row.requests;
    if (row.cache === 'snapshot') day.snapshot += row.requests;
    if (row.cache !== 'none') day.answered += row.requests;
  }
  return [...days.values()].sort(
    (a, b) => b.date.localeCompare(a.date) || a.entry.localeCompare(b.entry)
  );
}

// Whether any day has something in the Other column, so the table only grows
// it when there is something to put in it.
export function hasOtherSource(days: DayRow[]): boolean {
  return days.some((day) => (day.bySource.other ?? 0) > 0);
}

export interface TermTotal {
  term: string;
  requests: number;
}

// Terms of one kind summed across entry points, busiest first, ties by
// name, cut to a readable top. Kinds stay apart: a query that filters on a
// motif and orders by it counts once in each, and merging them would count
// it twice.
export function topTerms(rows: QueryTermRow[], kind: string, limit: number): TermTotal[] {
  const totals = new Map<string, number>();
  for (const row of rows) {
    if (row.kind !== kind) continue;
    totals.set(row.term, (totals.get(row.term) ?? 0) + row.requests);
  }
  return [...totals.entries()]
    .map(([term, requests]) => ({ term, requests }))
    .sort((a, b) => b.requests - a.requests || a.term.localeCompare(b.term))
    .slice(0, limit);
}
