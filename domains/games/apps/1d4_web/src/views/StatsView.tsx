import { useQuery } from '@tanstack/react-query';
import { getQueryStats, getQueryTerms, STATS_WINDOW_DAYS } from '../api';
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

const n = (value: number) => value.toLocaleString();

// How the query surface gets used (MoonBase#1465): queries and aggregates per
// day split by where they came from and how they went, and which parts of
// ChessQL people reach for. Counts come from the stats service's daily
// rollup of one_d4's query events, so this fetches once per mount.
export default function StatsView() {
  const stats = useQuery<QueryStats>({
    queryKey: ['queryStats'],
    queryFn: getQueryStats,
  });
  const terms = useQuery<QueryTerms>({
    queryKey: ['queryTerms'],
    queryFn: getQueryTerms,
  });

  if (stats.isPending || terms.isPending) {
    return <div className="loading">Loading stats…</div>;
  }
  if (stats.isError) {
    return (
      <div className="panel">
        <p className="message">Stats are not available right now.</p>
        <p className="panel-note">{stats.error.message}</p>
      </div>
    );
  }

  const days = rollupDays(stats.data.rows);
  const sources: string[] = hasOtherSource(stats.data.rows)
    ? [...SOURCES, 'other']
    : [...SOURCES];
  const window = stats.data.days ?? STATS_WINDOW_DAYS;

  return (
    <div className="stats-view">
      <div className="panel">
        <h2>Queries by day — last {window} days</h2>
        <p className="panel-note">
          One row per day and entry point. Snapshot is the first-page query
          answered from the warmed cache; the rest ran live.
        </p>
        <div className="table-wrap">
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
                  <td className="nowrap">{day.date}</td>
                  <td>{day.entry}</td>
                  <td>{n(day.total)}</td>
                  {sources.map((source) => (
                    <td key={source}>{n(day.bySource[source] ?? 0)}</td>
                  ))}
                  <td>{n(day.invalid)}</td>
                  <td>{n(day.failed)}</td>
                  <td>
                    {day.entry === 'query' && day.snapshot + day.live > 0
                      ? `${n(day.snapshot)} of ${n(day.snapshot + day.live)}`
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
      </div>

      <div className="panel">
        <h2>What queries use — last {window} days</h2>
        <p className="panel-note">
          Names only, never values: which fields queries filter on, which
          motifs they ask for (ORDER BY included), and which columns
          aggregates group by.
        </p>
        {terms.isError ? (
          <p className="message">Term usage is not available right now.</p>
        ) : (
          <div className="stats-terms">
            {TERM_KINDS.map((kind) => {
              const top = topTerms(terms.data.rows, kind, TOP_TERMS);
              return (
                <div key={kind} className="stats-terms-kind">
                  <h3>{KIND_LABELS[kind]}</h3>
                  {top.length === 0 ? (
                    <p className="text-muted">none yet</p>
                  ) : (
                    <table className="request-status-table" data-testid={`terms-${kind}`}>
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
        )}
      </div>
    </div>
  );
}
