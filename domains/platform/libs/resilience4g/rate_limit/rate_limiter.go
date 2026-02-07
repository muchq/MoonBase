package rate_limit

type RateLimiterConfig interface {
	GetMaxTokens() int64
	GetRefillRate() int64
	GetOpCost() int64
}

type DefaultRateLimitConfig struct {
	MaxTokens  int64
	RefillRate int64
	OpCost     int64
}

func (c *DefaultRateLimitConfig) GetMaxTokens() int64 {
	return c.MaxTokens
}

func (c *DefaultRateLimitConfig) GetRefillRate() int64 {
	return c.RefillRate
}

func (c *DefaultRateLimitConfig) GetOpCost() int64 {
	return c.OpCost
}

type RateLimiterInterface interface {
	Allow(cost int64) bool
}

type RateLimiterFactory interface {
	NewRateLimiter(config RateLimiterConfig) (RateLimiterInterface, error)
}
