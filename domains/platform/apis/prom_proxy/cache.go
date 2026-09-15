package prom_proxy

import (
	"sync"
	"time"
)

// Prometheus is scraped every 15s (scrapeInterval), so a response assembled
// less than one interval ago cannot differ from one assembled now — every
// query behind it reads the same samples. The dashboard fires three requests
// per page and re-polls every 30s in every open tab, so without this a second
// viewer doubles the query load to re-derive numbers already on hand (#1556).
//
// One interval and no more: the page is a live view, and a stale tile is
// worse than a slow one.
const cacheTTL = 15 * time.Second

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

// get reports a live entry. A nil cache always misses: tests construct a
// MetricsHandler directly, and every one of them expects its request to reach
// Prometheus rather than a neighbour's cached answer.
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
func scalarCacheKey(service string, view MetricView, timeRange TimeRange) string {
	return "scalar|" + service + "|" + string(view) + "|" + string(timeRange)
}

func seriesCacheKey(service, timeRange string) string {
	return "series|" + service + "|" + timeRange
}
