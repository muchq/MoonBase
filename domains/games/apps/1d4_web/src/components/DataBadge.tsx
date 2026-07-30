import type { DataAvailability } from '../types';

/**
 * How long until `expiresAt`, in whole days or hours. Anything already past is
 * reported as "soon" rather than as a negative — the sweep runs hourly, so a
 * lapsed deadline just means the worker hasn't come around yet.
 */
export function formatCountdown(expiresAt: number, now: number): string {
  const seconds = expiresAt - now / 1000;
  if (seconds <= 0) return 'soon';
  const hours = Math.floor(seconds / 3600);
  if (hours < 1) return '<1h';
  if (hours < 48) return `${hours}h`;
  return `${Math.floor(hours / 24)}d`;
}

export default function DataBadge({
  data,
  now = Date.now(),
}: {
  data: DataAvailability | null | undefined;
  now?: number;
}) {
  if (!data) return <span className="text-muted">—</span>;

  if (data.status === 'EXPIRED') {
    return (
      <span className="data-badge expired" title="Retention has deleted this request's games">
        Pruned
      </span>
    );
  }

  if (data.status === 'UNKNOWN') {
    return (
      <span className="data-badge unknown" title="Could not read this request's month range">
        Unknown
      </span>
    );
  }

  const countdown =
    data.expiresAt === null ? null : formatCountdown(data.expiresAt, now);

  if (data.status === 'PARTIAL') {
    return (
      <span
        className="data-badge partial"
        title={`${data.monthsAvailable} of ${data.monthsTotal} months still indexed`}
      >
        {data.monthsAvailable}/{data.monthsTotal} months
        {countdown && <span className="data-badge-note">{countdown} left</span>}
      </span>
    );
  }

  return (
    <span className="data-badge available" title="All of this request's games are still indexed">
      Indexed
      {countdown && <span className="data-badge-note">{countdown} left</span>}
    </span>
  );
}
