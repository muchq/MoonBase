package prom_proxy

import (
	"context"
	"sync"
	"time"
)

// A tile at most one scrape interval behind is current enough for a live view:
// that is already the floor on how fresh any of these numbers can be. The
// cache spends up to that much staleness to keep a second viewer from
// re-deriving numbers that are on hand — the dashboard fires three requests
// per page and re-polls every 30s in every open tab (#1556).
//
// It is a bound, not an identity: scrapes are staggered per target, and the
// windowed queries behind a tile move with the clock even between samples. So
// one interval and no more, because the page is a live view and a stale tile
// is worse than a slow one.
//
// Tied to the scrape interval rather than spelling 15s again, so the two
// cannot drift apart when prometheus.yml changes.
const cacheTTL = scrapeInterval

// responseCache memoizes assembled responses, keyed by everything that
// changes the answer — see scalarCacheKey and seriesCacheKey.
//
// A hit hands back the value the first caller assembled, so a cached response
// carries the Timestamp it was built at rather than the time it was served.
// That is the honest reading: it says when the numbers were fetched, which is
// what a viewer deciding whether to trust them needs.
type responseCache struct {
	mu      sync.Mutex
	entries map[string]cacheEntry
	// Injectable so the expiry test can move time rather than sleep through
	// a real TTL. Set before the handler serves anything.
	now func() time.Time
}

type cacheEntry struct {
	value   any
	expires time.Time
}

func newResponseCache() *responseCache {
	return &responseCache{entries: map[string]cacheEntry{}, now: time.Now}
}

// get reports a live entry. A nil cache always misses, which makes a
// MetricsHandler built without one a working handler that always queries.
func (c *responseCache) get(key string) (any, bool) {
	if c == nil {
		return nil, false
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	entry, ok := c.entries[key]
	if !ok || !c.now().Before(entry.expires) {
		return nil, false
	}
	return entry.value, true
}

// put takes ownership: the value is handed to every later get, so callers
// must treat it as frozen once it is in here. Nothing enforces that, and the
// uncached host handler next door does post-process a response in place.
func (c *responseCache) put(key string, value any) {
	if c == nil {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	now := c.now()
	// Swept on write. The key space is (services x views x ranges) — under a
	// hundred entries — so this costs nothing, and it keeps a service that
	// was renamed out of the registry from pinning its last response for the
	// life of the process.
	for k, entry := range c.entries {
		if !now.Before(entry.expires) {
			delete(c.entries, k)
		}
	}
	c.entries[key] = cacheEntry{value: value, expires: now.Add(cacheTTL)}
}

// The two key builders. Every input that changes a byte of the response is in
// the key: the service, the view a counter tile is read in, and the range
// every windowed tile now reads over (#1507). Miss one and the dashboard
// serves one window's numbers on another window's page.
//
// Only the per-service routes are keyed here. The host and container routes
// are polled by every tab too and would benefit, but the host timeseries
// route answers a failed scrape with a 500 rather than the zeros-with-200 the
// service routes promise, so caching it needs a rule about which failures are
// storable that this pair never had to state.
// cacheIfComplete stores a response unless the request it was assembled for
// was abandoned or ran out of time.
//
// Those two are the one failure the cache must not keep. A viewer who switches
// service tabs mid-load, reloads, or closes the tab cancels the request's
// context, and every query in flight and every one not yet started fails at
// once — which assembles a whole page of zeros. That is the outage contract,
// and it was that one request's problem until the cache made it everyone's: a
// service that is serving fine would read as dead, instantly and with a fresh
// timestamp, for the next viewer and the whole TTL. The 30s deadline does the
// same thing with no client involved at all.
//
// An ordinary failed query is different and is stored: a single tile whose
// query always fails would otherwise disable the cache for that page forever,
// and one unlucky assembly costs at most one interval.
func cacheIfComplete(ctx context.Context, cache *responseCache, key string, response any) {
	if ctx.Err() != nil {
		return
	}
	cache.put(key, response)
}

func scalarCacheKey(service string, view MetricView, timeRange TimeRange) string {
	return "scalar|" + service + "|" + string(view) + "|" + string(timeRange)
}

func seriesCacheKey(service, timeRange string) string {
	return "series|" + service + "|" + timeRange
}
