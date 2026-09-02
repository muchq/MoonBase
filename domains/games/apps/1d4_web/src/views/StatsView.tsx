import { useQuery } from '@tanstack/react-query';
import { getQueryStats, getQueryTerms } from '../api';
import type { QueryStats, QueryTerms } from '../types';
import {
  hasOtherSource,
  KIND_LABELS,
  rollupDays,
  SOURCE_LABELS,
  SOURCES,
  TERM_KINDS,
  topTerms,
} from '../statsRollup';

const TOP_TERMS = 15;

// The aggregator's pass is every 15 minutes; refetching on every window
// focus would only ever show the same numbers.
const STALE_MS = 5 * 60_000;

const n = (value: number) => value.toLocaleString();

// How the query surface gets used (MoonBase#1465): queries and aggregates per
// day split by where they came from and how they went, and which parts of
// ChessQL people reach for. Two endpoints, two panels, each with its own
// loading and error state, so one being down does not blank the other.
export default function StatsView() {
  const stats = useQuery<QueryStats>({
    queryKey: ['queryStats'],
    queryFn: getQueryStats,
    staleTime: STALE_MS,
  });
  const terms = useQuery<QueryTerms>({
    queryKey: ['queryTerms'],
    queryFn: getQueryTerms,
    staleTime: STALE_MS,
  });

  return (
    <div className="stats-view">
      <div className="panel">
        {stats.isPending ? (
          <div className="loading">Loading stats…</div>
        ) : stats.isError ? (
          <>
            <p className="message">Stats are not available right now.</p>
            <p className="panel-note">{stats.error.message}</p>
          </>
        ) : (
          <QueriesByDay data={stats.data} />
        )}
      </div>

      <div className="panel">
        {terms.isPending ? (
          <div className="loading">Loading term usage…</div>
        ) : terms.isError ? (
          <p className="message">Term usage is not available right now.</p>
        ) : (
          <TermUsage data={terms.data} />
        )}
      </div>
    </div>
  );
}

function QueriesByDay({ data }: { data: QueryStats }) {
  const days = rollupDays(data.rows);
  const sources: string[] = hasOtherSource(days) ? [...SOURCES, 'other'] : [...SOURCES];
  return (
    <>
      <h2>Queries by day — last {data.days} days</h2>
      <p className="panel-note">
        One row per day and entry point. Snapshot is the first-page query
        answered from the warmed cache, out of the queries that ran at all.
      </p>
      <div className="table-wrap">
        {/* data-label drives the stacked card layout under 640px, where the
            header row is hidden and each cell carries its own name. */}
        <table className="request-status-table" data-testid="queries-by-day">
          <thead>
            <tr>
              <th>Date</th>
              <th>Entry</th>
              <th>Requests</th>
              {sources.map((source) => (
                <th key={source}>{SOURCE_LABELS[source] ?? source}</th>
              ))}
              <th>Invalid</th>
              <th>Failed</th>
              <th>Snapshot</th>
            </tr>
          </thead>
          <tbody>
            {days.map((day) => (
              <tr key={`${day.date} ${day.entry}`}>
                <td data-label="Date" className="nowrap">
                  {day.date}
                </td>
                <td data-label="Entry">{day.entry}</td>
                <td data-label="Requests">{n(day.total)}</td>
                {sources.map((source) => (
                  <td key={source} data-label={SOURCE_LABELS[source] ?? source}>
                    {n(day.bySource[source] ?? 0)}
                  </td>
                ))}
                <td data-label="Invalid">{n(day.invalid)}</td>
                <td data-label="Failed">{n(day.failed)}</td>
                <td data-label="Snapshot">
                  {day.entry === 'query' && day.answered > 0
                    ? `${n(day.snapshot)} of ${n(day.answered)}`
                    : '—'}
                </td>
              </tr>
            ))}
            {days.length === 0 && (
              <tr>
                <td colSpan={6 + sources.length} className="empty">
                  No queries aggregated yet.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </>
  );
}

function TermUsage({ data }: { data: QueryTerms }) {
  return (
    <>
      <h2>What queries use — last {data.days} days</h2>
      <p className="panel-note">
        Names only, never values: which fields queries filter on, which
        motifs they ask for and order by, and which columns aggregates group
        by.
      </p>
      <div className="stats-terms">
        {TERM_KINDS.map((kind) => {
          const top = topTerms(data.rows, kind, TOP_TERMS);
          return (
            <div key={kind} className="stats-terms-kind">
              <h3>{KIND_LABELS[kind]}</h3>
              {top.length === 0 ? (
                <p className="text-muted">none yet</p>
              ) : (
                <table className="stats-terms-table" data-testid={`terms-${kind}`}>
                  <tbody>
                    {top.map((row) => (
                      <tr key={row.term}>
                        <td>
                          <code>{row.term}</code>
                        </td>
                        <td>{n(row.requests)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              )}
            </div>
          );
        })}
      </div>
    </>
  );
}
