package rate_limit

import (
	"github.com/stretchr/testify/assert"
	"testing"
)

var factory TokenBucketRateLimiterFactory = TokenBucketRateLimiterFactory{}

func TestFactoryAllowsValidConfig(t *testing.T) {
	config := &DefaultRateLimitConfig{
		maxTokens:  1,
		refillRate: 1,
		opCost:     1,
	}

	_, err := factory.NewRateLimiter(config)
	assert.Nil(t, err, "err should be nil for valid config")
}

func TestFactoryValidatesMaxTokens(t *testing.T) {
	config := &DefaultRateLimitConfig{
		maxTokens:  0,
		refillRate: 1,
		opCost:     1,
	}

	_, err := factory.NewRateLimiter(config)
	assert.EqualError(t, err, "max tokens must be positive")
}

func TestFactoryValidatesRefillRate(t *testing.T) {
	config := &DefaultRateLimitConfig{
		maxTokens:  1,
		refillRate: 0,
		opCost:     1,
	}

	_, err := factory.NewRateLimiter(config)
	assert.EqualError(t, err, "refill rate must be positive")
}

func TestFactoryValidatesOpCost(t *testing.T) {
	config := &DefaultRateLimitConfig{
		maxTokens:  1,
		refillRate: 1,
		opCost:     0,
	}

	_, err := factory.NewRateLimiter(config)
	assert.EqualError(t, err, "op cost must be positive")
}
