package rate_limit

import (
	"github.com/muchq/moonbase/go/clock"
	"github.com/muchq/moonbase/go/mucks"
	"github.com/stretchr/testify/assert"
	"net/http"
	"net/http/httptest"
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
