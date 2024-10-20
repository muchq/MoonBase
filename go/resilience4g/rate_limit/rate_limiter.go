package rate_limit

type RateLimiterConfig interface {
	GetMaxTokens() int64
	GetRefillRate() int64
	GetOpCost() int64
}

type DefaultRateLimitConfig struct {
	maxTokens  int64
	refillRate int64
	opCost     int64
}

func (c *DefaultRateLimitConfig) GetMaxTokens() int64 {
	return c.maxTokens
}

func (c *DefaultRateLimitConfig) GetRefillRate() int64 {
	return c.refillRate
}

func (c *DefaultRateLimitConfig) GetOpCost() int64 {
	return c.opCost
}

type RateLimiterInterface interface {
	Allow(cost int64) bool
}

type RateLimiterFactory interface {
	NewRateLimiter(config RateLimiterConfig) (RateLimiterInterface, error)
}
