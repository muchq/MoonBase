package prom_proxy

import (
	"strconv"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// A clock the test moves by hand. The alternative is sleeping through a real
// 15s TTL, which would make this the slowest test in the repo.
type fakeClock struct{ t time.Time }

func (c *fakeClock) now() time.Time      { return c.t }
func (c *fakeClock) add(d time.Duration) { c.t = c.t.Add(d) }

func testCache() (*responseCache, *fakeClock) {
	clock := &fakeClock{t: time.Date(2026, 9, 15, 12, 0, 0, 0, time.UTC)}
	cache := newResponseCache()
	cache.now = clock.now
	return cache, clock
}

// The TTL's whole justification is that it does not outlast the interval at
// which the data behind it can change. Every other test here takes cacheTTL as
// given, so this is the only place that value is pinned to anything.
func TestResponseCache_TTLIsOneScrapeInterval(t *testing.T) {
	assert.Equal(t, scrapeInterval, cacheTTL,
		"the cache may not hold a response longer than it takes for a newer one to exist")
}

func TestResponseCache_HitMissAndExpiry(t *testing.T) {
	cache, clock := testCache()

	_, ok := cache.get("k")
	assert.False(t, ok, "an empty cache reported a hit")

	cache.put("k", "v")
	value, ok := cache.get("k")
	require.True(t, ok, "the value just written was not found")
	assert.Equal(t, "v", value)

	// The last instant the entry is still good. An off-by-one here widens the
	// staleness bound past the scrape interval the TTL is pinned to.
	clock.add(cacheTTL - time.Nanosecond)
	_, ok = cache.get("k")
	assert.True(t, ok, "the entry expired inside its TTL")

	clock.add(time.Nanosecond)
	_, ok = cache.get("k")
	assert.False(t, ok, "the entry was served at exactly its expiry")
}

// An unbounded map keyed on service names would keep the last response of
// every service that ever existed — including ones since renamed out of the
// registry — for the life of the process.
func TestResponseCache_PutSweepsExpiredEntries(t *testing.T) {
	cache, clock := testCache()
	cache.put("stale", "old")

	clock.add(cacheTTL)
	cache.put("fresh", "new")

	cache.mu.Lock()
	defer cache.mu.Unlock()
	assert.Equal(t, []string{"fresh"}, keysOf(cache),
		"the expired entry survived a later write")
}

func keysOf(c *responseCache) []string {
	keys := make([]string, 0, len(c.entries))
	for k := range c.entries {
		keys = append(keys, k)
	}
	return keys
}

// The handlers hold no lock while fanning out, so two requests for different
// services can write at once. Only meaningful under -race, which CI runs.
func TestResponseCache_ConcurrentUse(t *testing.T) {
	cache, _ := testCache()
	var wg sync.WaitGroup
	for i := 0; i < 32; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			key := "k" + strconv.Itoa(i%4)
			cache.put(key, i)
			cache.get(key)
		}(i)
	}
	wg.Wait()
}

// A nil cache is a working handler that always queries — every test that
// builds a MetricsHandler as a struct literal depends on it.
func TestResponseCache_NilAlwaysMisses(t *testing.T) {
	var cache *responseCache
	cache.put("k", "v")
	_, ok := cache.get("k")
	assert.False(t, ok, "a nil cache reported a hit")
}

// Every input that changes a byte of the response has to change the key.
// Collapse any one of them and the dashboard serves one page's numbers on
// another's — the failure mode a cache introduces that no other part of this
// service can.
func TestCacheKeys_DistinguishEveryInput(t *testing.T) {
	base := scalarCacheKey("games_hub", ViewCount, LastDay)
	for _, tt := range []struct {
		what string
		key  string
	}{
		{"service", scalarCacheKey("one_d4", ViewCount, LastDay)},
		{"view", scalarCacheKey("games_hub", ViewRate, LastDay)},
		{"range", scalarCacheKey("games_hub", ViewCount, LastWeek)},
	} {
		assert.NotEqual(t, base, tt.key, "the %s does not change the scalar key", tt.what)
	}
	assert.Equal(t, base, scalarCacheKey("games_hub", ViewCount, LastDay),
		"the same request built two different keys")

	series := seriesCacheKey("games_hub", "1d")
	assert.NotEqual(t, series, seriesCacheKey("one_d4", "1d"), "the service does not change the series key")
	assert.NotEqual(t, series, seriesCacheKey("games_hub", "7d"), "the range does not change the series key")

	// The two routes answer different shapes on the same (service, range);
	// a shared key would serve a TimeSeriesResponse to a tile request.
	assert.NotEqual(t, scalarCacheKey("games_hub", ViewCount, LastDay), seriesCacheKey("games_hub", "1d"),
		"the scalar and series routes share a key")
}
