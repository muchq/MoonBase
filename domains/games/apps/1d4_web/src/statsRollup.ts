import type { QueryStatRow, QueryTermRow } from './types';

export const SOURCES = ['ui', 'mcp', 'api'] as const;

export const SOURCE_LABELS: Record<string, string> = {
  ui: 'Web',
  mcp: 'MCP',
  api: 'API',
  other: 'Other',
};

export const TERM_KINDS = ['field', 'motif', 'group_by'] as const;

export const KIND_LABELS: Record<string, string> = {
  field: 'Fields',
  motif: 'Motifs',
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
  live: number;
}

// One row per day and entry point, newest first, with the source split, the
// error split, and (queries only) how many were answered from the first-page
// snapshot rather than run live. "other" is what the aggregator files a word
// this build does not know under; it shows so drift is visible, not hidden.
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
        live: 0,
      };
      days.set(key, day);
    }
    day.total += row.requests;
    day.bySource[row.source] = (day.bySource[row.source] ?? 0) + row.requests;
    if (row.outcome === 'invalid') day.invalid += row.requests;
    if (row.outcome === 'failed') day.failed += row.requests;
    if (row.cache === 'snapshot') day.snapshot += row.requests;
    if (row.cache === 'live') day.live += row.requests;
  }
  return [...days.values()].sort(
    (a, b) => b.date.localeCompare(a.date) || a.entry.localeCompare(b.entry)
  );
}

// Whether any row carries a source outside the known three, so the table only
// grows an "Other" column when there is something to put in it.
export function hasOtherSource(rows: QueryStatRow[]): boolean {
  return rows.some((row) => !SOURCES.includes(row.source as (typeof SOURCES)[number]));
}

export interface TermTotal {
  term: string;
  requests: number;
}

// Terms of one kind summed across entry points, busiest first, cut to a
// readable top. order_by motifs fold into motifs: they are the same
// vocabulary, and "which motifs get asked about" is the question.
export function topTerms(rows: QueryTermRow[], kind: string, limit: number): TermTotal[] {
  const totals = new Map<string, number>();
  for (const row of rows) {
    const rowKind = row.kind === 'order_by' ? 'motif' : row.kind;
    if (rowKind !== kind) continue;
    totals.set(row.term, (totals.get(row.term) ?? 0) + row.requests);
  }
  return [...totals.entries()]
    .map(([term, requests]) => ({ term, requests }))
    .sort((a, b) => b.requests - a.requests || a.term.localeCompare(b.term))
    .slice(0, limit);
}
