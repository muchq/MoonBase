package rate_limit

type RateLimiterConfig struct {
	MaxTokens  int64
	RefillRate int64
	OpCost     int
}

type RateLimiterInterface interface {
	Allow(cost int) bool
}

type RateLimiterFactory interface {
	NewRateLimiter(config RateLimiterConfig) RateLimiterInterface
}
