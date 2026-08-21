// Recent links live only in this browser: the API has no list endpoint, and
// a shortener doesn't need accounts to be useful. localStorage can throw
// (private windows, blocked storage) — every touch is wrapped, and the app
// works identically with none.
export interface RecentLink {
  slug: string;
  longUrl: string;
  expiresAt: number;
}

const KEY = 'r3dr.recent';
const MAX = 5;

// Slugs are exactly 3, 6, or 11 base64url chars (the encoder's widths). A
// hand-edited store must not render arbitrary text — or never-resolvable
// lengths — into links.
const SLUG_SHAPE = /^(?:[A-Za-z0-9_-]{3}|[A-Za-z0-9_-]{6}|[A-Za-z0-9_-]{11})$/;

export function loadRecent(now: number): RecentLink[] {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const parsed: unknown = JSON.parse(raw);
    if (!Array.isArray(parsed)) return [];
    return parsed
      .filter(
        (entry): entry is RecentLink =>
          entry !== null &&
          typeof entry === 'object' &&
          typeof (entry as RecentLink).slug === 'string' &&
          typeof (entry as RecentLink).longUrl === 'string' &&
          typeof (entry as RecentLink).expiresAt === 'number'
      )
      .filter((entry) => SLUG_SHAPE.test(entry.slug) && entry.expiresAt > now)
      .slice(0, MAX);
  } catch (_) {
    return [];
  }
}

/** Newest first, deduped by slug, capped. Returns the list to render. */
export function addRecent(links: RecentLink[], link: RecentLink): RecentLink[] {
  const next = [link, ...links.filter((l) => l.slug !== link.slug)].slice(0, MAX);
  try {
    localStorage.setItem(KEY, JSON.stringify(next));
  } catch (_) {
    // storage unavailable; the in-memory list still renders
  }
  return next;
}

export function clearRecent(): void {
  try {
    localStorage.removeItem(KEY);
  } catch (_) {
    // nothing to clear
  }
}
