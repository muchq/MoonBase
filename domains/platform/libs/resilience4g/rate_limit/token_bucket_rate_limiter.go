package rate_limit

import (
	"errors"
	"github.com/muchq/moonbase/domains/platform/libs/clock"
	"sync"
)

// TokenBucketRateLimiter should always be passed and accessed by pointer
// because it contains a sync.Mutex field which cannot be safely copied.
// Use NewInMemoryRateLimiter when constructing instances.
type TokenBucketRateLimiter struct {
	Config        RateLimiterConfig
	LastRefill    int64
	CurrentTokens float64
	Clock         clock.Clock
	Lock          sync.Mutex
}

func (rl *TokenBucketRateLimiter) Allow(cost int64) bool {
	rl.Lock.Lock()
	defer rl.Lock.Unlock()
	rl.refill()
	floatCost := float64(cost)

	if rl.CurrentTokens >= floatCost {
		rl.CurrentTokens -= floatCost
		return true
	}
	return false
}

func (rl *TokenBucketRateLimiter) refill() {
	now := rl.Clock.Now().UnixNano()
	toAdd := float64((now - rl.LastRefill) * rl.Config.GetRefillRate() / 1e9)
	if toAdd < 1.0 {
		return
	}
	rl.CurrentTokens = min(rl.CurrentTokens+toAdd, float64(rl.Config.GetMaxTokens()))
	rl.LastRefill = now
}

type TokenBucketRateLimiterFactory struct {
}

func checkPositive(value int64, message string) error {
	if value <= 0 {
		return errors.New(message)
	}
	return nil
}

func (TokenBucketRateLimiterFactory) NewRateLimiter(config RateLimiterConfig) (RateLimiterInterface, error) {
	if config.GetMaxTokens() <= 0 {
		return nil, errors.New("max tokens must be positive")
	}
	if config.GetRefillRate() <= 0 {
		return nil, errors.New("refill rate must be positive")
	}
	if config.GetOpCost() <= 0 {
		return nil, errors.New("op cost must be positive")
	}

	clock := clock.NewSystemUtcClock()
	return &TokenBucketRateLimiter{
		Config:        config,
		CurrentTokens: float64(config.GetMaxTokens()),
		Clock:         clock,
		LastRefill:    clock.Now().UnixNano(),
	}, nil
}
