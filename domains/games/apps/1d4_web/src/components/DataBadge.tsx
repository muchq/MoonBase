import type { DataAvailability } from '../types';

/**
 * How long until `expiresAt`, in whole days or hours. Anything already past is
 * reported as "soon" rather than as a negative — the sweep runs hourly, so a
 * lapsed deadline just means the worker hasn't come around yet.
 */
/**
 * Reads as a whole phrase — "3d left", "expiring" — because a lapsed deadline has no number to
 * show. The sweep runs hourly, so a deadline in the past just means the worker hasn't come around
 * yet; rendering that as a negative, or as "soon left", would be worse than saying nothing precise.
 */
export function formatCountdown(expiresAt: number, now: number): string {
  const seconds = expiresAt - now / 1000;
  if (seconds <= 0) return 'expiring';
  const hours = Math.floor(seconds / 3600);
  if (hours < 1) return '<1h left';
  if (hours < 48) return `${hours}h left`;
  return `${Math.floor(hours / 24)}d left`;
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

  // `== null` on purpose: the server omits expiresAt rather than sending null, so this has to
  // catch undefined too.
  const countdown =
    data.expiresAt == null ? null : formatCountdown(data.expiresAt, now);

  if (data.status === 'PARTIAL') {
    return (
      <span
        className="data-badge partial"
        title={`${data.monthsAvailable} of ${data.monthsTotal} months still indexed`}
      >
        {data.monthsAvailable}/{data.monthsTotal} months
        {countdown && <span className="data-badge-note">{countdown}</span>}
      </span>
    );
  }

  if (data.status === 'AVAILABLE') {
    return (
      <span className="data-badge available" title="All of this request's games are still indexed">
        Indexed
        {countdown && <span className="data-badge-note">{countdown}</span>}
      </span>
    );
  }

  // UNKNOWN, and anything a newer server sends that this build doesn't recognize. Falling through
  // to the reassuring "Indexed" badge would be fail-open in a component whose job is to warn.
  return (
    <span className="data-badge unknown" title="Could not determine whether this data is still indexed">
      Unknown
    </span>
  );
}
