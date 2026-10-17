package rate_limit

import (
	"fmt"
	"github.com/muchq/moonbase/domains/platform/libs/clock"
	"github.com/muchq/moonbase/domains/platform/libs/mucks"
	"github.com/stretchr/testify/assert"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
)

func Setup() (*mucks.Mucks, *httptest.Server, *http.Client) {
	m := mucks.NewMucks()
	s := httptest.NewServer(m)
	c := s.Client()
	return m, s, c
}

func TestRateLimiterMiddleware(t *testing.T) {
	m, s, client := Setup()
	defer s.Close()
	testClock := clock.NewTestClock()
	factory := &RLFactory{testClock}
	config := &DefaultRateLimitConfig{
		MaxTokens:  2,
		RefillRate: 1,
		OpCost:     2,
	}

	m.Add(NewRateLimiterMiddleware(factory, ConstKeyExtractor{}, config))
	m.HandleFunc("GET /foo", FooHandler)

	// First call should work since OpCost equals MaxTokens
	response, err := client.Get(s.URL + "/foo")
	assert.Nil(t, err, "error on Get")
	assert.Equal(t, response.StatusCode, 200, "should be OK")

	// Second call before Tick should fail with 429
	response, err = client.Get(s.URL + "/foo")
	assert.Nil(t, err, "error on Get")
	assert.Equal(t, response.StatusCode, 429, "should be rate limit exceeded")

	// Time passes, but not enough
	testClock.Tick(1)

	// Third call should fail with 429
	response, err = client.Get(s.URL + "/foo")
	assert.Nil(t, err, "error on Get")
	assert.Equal(t, response.StatusCode, 429, "should be rate limit exceeded")

	// Now enough time passes
	testClock.Tick(1)

	// Fourth call should succeed again
	response, err = client.Get(s.URL + "/foo")
	assert.Nil(t, err, "error on Get")
	assert.Equal(t, response.StatusCode, 200, "should be ok again")
}

type RLFactory struct {
	TestClock clock.Clock
}

func (f *RLFactory) NewRateLimiter(config RateLimiterConfig) (RateLimiterInterface, error) {
	return &TokenBucketRateLimiter{
		Config:        config,
		CurrentTokens: float64(config.GetMaxTokens()),
		Clock:         f.TestClock,
		LastRefill:    f.TestClock.Now().UnixNano(),
	}, nil
}

type FooResponse struct {
	Name  string `json:"name"`
	Value int    `json:"value"`
}

func FooHandler(w http.ResponseWriter, _ *http.Request) {
	foo := FooResponse{
		Name:  "bonk",
		Value: 100,
	}

	mucks.JsonOk(w, foo)
}

func peerRequest(remoteAddr string, xff ...string) *http.Request {
	r := httptest.NewRequest("GET", "/foo", nil)
	r.RemoteAddr = remoteAddr
	for _, v := range xff {
		r.Header.Add("X-Forwarded-For", v)
	}
	return r
}

// The header starts life client-authored, so a key read from it is the
// client's to choose: a fresh value per request is a fresh bucket.
func TestPeerIpKeyExtractorIgnoresForwardedFor(t *testing.T) {
	e := PeerIpKeyExtractor{}
	assert.Equal(t, "203.0.113.9", e.Apply(peerRequest("203.0.113.9:5000")))
	assert.Equal(t, "203.0.113.9", e.Apply(peerRequest("203.0.113.9:5000", "6.6.6.6")))
	assert.Equal(t, "203.0.113.9", e.Apply(peerRequest("203.0.113.9:5000", "6.6.6.6, 7.7.7.7")))
	assert.Equal(t, "203.0.113.9", e.Apply(peerRequest("203.0.113.9:5000", "6.6.6.6", "7.7.7.7")))
}

// Keys are canonical addresses, so one client is one key however the
// listener spells it: a dual-stack socket reports v4 clients as mapped v6.
func TestPeerIpKeyExtractorCanonicalizesThePeer(t *testing.T) {
	e := PeerIpKeyExtractor{}
	assert.Equal(t, "2001:db8::1", e.Apply(peerRequest("[2001:DB8::1]:443")))
	assert.Equal(t, "203.0.113.9", e.Apply(peerRequest("[::ffff:203.0.113.9]:443")))
	assert.Equal(t, "fe80::1", e.Apply(peerRequest("[fe80::1%eth0]:443")))
}

func TestPeerIpKeyExtractorWithNoUsablePeerIsEmpty(t *testing.T) {
	e := PeerIpKeyExtractor{}
	assert.Equal(t, "", e.Apply(peerRequest("")))
	assert.Equal(t, "", e.Apply(peerRequest("not-an-address")))
	assert.Equal(t, "", e.Apply(peerRequest("not-an-ip:80", "203.0.113.9")))
}

func oneShotMiddleware() mucks.Middleware {
	config := &DefaultRateLimitConfig{MaxTokens: 1, RefillRate: 1, OpCost: 1}
	return NewRateLimiterMiddleware(&RLFactory{clock.NewTestClock()}, PeerIpKeyExtractor{}, config)
}

func statusOf(h http.HandlerFunc, r *http.Request) int {
	w := httptest.NewRecorder()
	h(w, r)
	return w.Code
}

func TestASpoofedForwardedForDoesNotBuyAFreshBucket(t *testing.T) {
	h := oneShotMiddleware().Wrap(FooHandler)
	assert.Equal(t, 200, statusOf(h, peerRequest("203.0.113.9:5000", "6.6.6.6")))
	assert.Equal(t, 429, statusOf(h, peerRequest("203.0.113.9:5001", "7.7.7.7")))
}

// The control: distinct peers are distinct buckets, so the 429 above is
// the shared key and not a limiter that refuses everything.
func TestDistinctPeersGetTheirOwnBuckets(t *testing.T) {
	h := oneShotMiddleware().Wrap(FooHandler)
	assert.Equal(t, 200, statusOf(h, peerRequest("203.0.113.9:5000")))
	assert.Equal(t, 200, statusOf(h, peerRequest("198.51.100.7:5000")))
}

// Every new key writes the limiter map under its lock, so every read must
// take the same lock.
func TestConcurrentNewKeysAreSafe(t *testing.T) {
	h := oneShotMiddleware().Wrap(FooHandler)
	var wg sync.WaitGroup
	for i := 0; i < 200; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			statusOf(h, peerRequest(fmt.Sprintf("10.0.%d.%d:5000", i/256, i%256)))
		}(i)
	}
	wg.Wait()
}
