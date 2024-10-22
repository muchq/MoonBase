package rate_limit

import (
	"github.com/stretchr/testify/assert"
	"testing"
	"time"
)

var factory TokenBucketRateLimiterFactory = TokenBucketRateLimiterFactory{}
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

type TestClock struct {
	unixSeconds int64
}

// Now implements the Clock interface
func (c *TestClock) Now() time.Time {
	return time.Unix(c.unixSeconds, 0)
}

func (c *TestClock) Tick(secs int64) {
	c.unixSeconds += secs
}

func TestLimiterAllowsRequestsIfCurrentTokensLessThanOpCost(t *testing.T) {
	testClock := &TestClock{}
	testClock.Tick(100)
}
