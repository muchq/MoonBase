// The API requires expiresAt (epoch millis), future, ceiling 30 days.
export interface ExpiryOption {
  label: string;
  ms: number;
}

const MINUTE = 60 * 1000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;

export const EXPIRY_OPTIONS: ExpiryOption[] = [
  { label: '1 hour', ms: HOUR },
  { label: '1 day', ms: DAY },
  { label: '7 days', ms: 7 * DAY },
  // Five minutes under the exact ceiling: the server checks against its own
  // clock, and a client running seconds fast must not turn this button into
  // a guaranteed 400.
  { label: '30 days', ms: 30 * DAY - 5 * MINUTE },
];

// v1 hardcoded 7 days; keep it as the default.
export const DEFAULT_EXPIRY = EXPIRY_OPTIONS[2];

/** "in 7 days" / "in 3 hours" / "in 20 minutes" — for expiry notes. */
export function describeExpiry(deltaMs: number): string {
  if (deltaMs <= 0) return 'expired';
  const minutes = Math.round(deltaMs / MINUTE);
  if (minutes < 60) return `in ${minutes} minute${minutes === 1 ? '' : 's'}`;
  const hours = Math.round(deltaMs / HOUR);
  if (hours < 48) return `in ${hours} hour${hours === 1 ? '' : 's'}`;
  const days = Math.round(deltaMs / DAY);
  return `in ${days} days`;
}
