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
      .filter((entry) => entry.expiresAt > now)
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
