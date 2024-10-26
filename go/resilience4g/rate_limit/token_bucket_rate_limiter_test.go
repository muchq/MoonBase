package rate_limit

import (
	"github.com/muchq/moonbase/go/clock"
	"github.com/stretchr/testify/assert"
	"testing"
)

var factory = TokenBucketRateLimiterFactory{}
var validConfig RateLimiterConfig = &DefaultRateLimitConfig{
	MaxTokens:  1,
	RefillRate: 1,
	OpCost:     1,
}

func TestFactoryAllowsValidConfig(t *testing.T) {
	_, err := factory.NewRateLimiter(validConfig)
	assert.Nil(t, err, "err should be nil for valid config")
}

func TestFactoryValidatesMaxTokens(t *testing.T) {
	config := &DefaultRateLimitConfig{
		MaxTokens:  0,
		RefillRate: 1,
		OpCost:     1,
	}

	_, err := factory.NewRateLimiter(config)
	assert.EqualError(t, err, "max tokens must be positive")
}

func TestFactoryValidatesRefillRate(t *testing.T) {
	config := &DefaultRateLimitConfig{
		MaxTokens:  1,
		RefillRate: 0,
		OpCost:     1,
	}

	_, err := factory.NewRateLimiter(config)
	assert.EqualError(t, err, "refill rate must be positive")
}

func TestFactoryValidatesOpCost(t *testing.T) {
	config := &DefaultRateLimitConfig{
		MaxTokens:  1,
		RefillRate: 1,
		OpCost:     0,
	}

	_, err := factory.NewRateLimiter(config)
	assert.EqualError(t, err, "op cost must be positive")
}

func TestLimiterAllowsRequestsIfCurrentTokensGreaterThanOpCost(t *testing.T) {
	testClock := clock.NewTestClock()
	limiter := &TokenBucketRateLimiter{
		Config:        validConfig,
		CurrentTokens: float64(validConfig.GetMaxTokens()),
		Clock:         testClock,
		LastRefill:    testClock.Now().UnixNano(),
	}

	assert.True(t, limiter.Allow(1), "op with cost 1 is allowed")
}

func TestLimiterDeniesRequestsIfCurrentTokensLessThanOpCost(t *testing.T) {
	testClock := clock.NewTestClock()
	limiter := &TokenBucketRateLimiter{
		Config:        validConfig,
		CurrentTokens: float64(validConfig.GetMaxTokens()),
		Clock:         testClock,
		LastRefill:    testClock.Now().UnixNano(),
	}

	assert.False(t, limiter.Allow(2), "op with cost 2 is not allowed")
}

func TestLimiterRefreshesTokens(t *testing.T) {
	testClock := clock.NewTestClock()
	limiter := &TokenBucketRateLimiter{
		Config:        validConfig,
		CurrentTokens: float64(validConfig.GetMaxTokens()),
		Clock:         testClock,
		LastRefill:    testClock.Now().UnixNano(),
	}

	assert.True(t, limiter.Allow(1), "op with cost 1 is allowed")
	assert.False(t, limiter.Allow(1), "tokens should be used")

	testClock.Tick(1)
	assert.True(t, limiter.Allow(1), "op with cost 1 is allowed again after 1 second")
}
