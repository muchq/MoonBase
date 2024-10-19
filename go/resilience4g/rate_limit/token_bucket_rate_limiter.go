package rate_limit

import (
	"sync"
	"time"
)

// TokenBucketRateLimiter should always be passed and accessed by pointer
// because it contains a sync.Mutex field which cannot be safely copied.
// Use NewInMemoryRateLimiter when constructing instances.
type TokenBucketRateLimiter struct {
	MaxTokens     int64
	RefillRate    int64
	LastRefill    int64
	CurrentTokens float64
	Lock          sync.Mutex
}

func (rl *TokenBucketRateLimiter) Allow(cost int) bool {
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
	now := time.Now().UnixNano()
	toAdd := float64((now - rl.LastRefill) * rl.RefillRate / 1e9)
	if toAdd < 1.0 {
		return
	}
	rl.CurrentTokens = min(rl.CurrentTokens+toAdd, float64(rl.MaxTokens))
	rl.LastRefill = now
}

type TokenBucketRateLimiterFactory struct {
}

func (TokenBucketRateLimiterFactory) NewRateLimiter(config RateLimiterConfig) RateLimiterInterface {
	return &TokenBucketRateLimiter{
		MaxTokens:     config.MaxTokens,
		RefillRate:    config.RefillRate,
		CurrentTokens: float64(config.MaxTokens),
		LastRefill:    time.Now().UnixNano(),
	}
}
