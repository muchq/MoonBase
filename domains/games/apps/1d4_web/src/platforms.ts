// The platforms the indexer serves: what the API stores, and what a person reads.
//
// One list, because the submit form and the games views have to agree. The
// column holds CHESS_COM — IndexRequestService canonicalizes before the row
// exists — and nobody wants to read that, so the label is the site's own name.
//
// Keep in step with IndexRequestService.SUPPORTED_PLATFORMS: an option here
// that the API refuses is a form that 400s on submit.

export interface Platform {
  /** As stored, and as one_d4_worker's archive registry is keyed. */
  value: string;
  label: string;
}

export const PLATFORMS: Platform[] = [
  { value: 'CHESS_COM', label: 'chess.com' },
  { value: 'LICHESS', label: 'lichess' },
];

const LABELS: Record<string, string> = Object.fromEntries(
  PLATFORMS.map((platform) => [platform.value, platform.label])
);

/**
 * How to spell a stored platform on screen.
 *
 * An unrecognized value is shown as stored rather than blanked: a row whose
 * platform this build has not heard of still came from somewhere, and an empty
 * cell would say it came from nowhere.
 */
export function platformLabel(value: string): string {
  return LABELS[value] ?? value;
}
