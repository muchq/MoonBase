package main

import "github.com/muchq/moonbase/domains/platform/libs/resilience4g/rate_limit"

const InternalError string = "internal error"
const NotFound string = "not found"

var CacheConfig = ShortenerCacheConfig{
	MaxItems:          1000,
	ExpirationMinutes: 5,
}

var ShortenRateLimiterConfig = &rate_limit.DefaultRateLimitConfig{
	MaxTokens:  2,
	RefillRate: 1,
	OpCost:     1,
}

var FallbackRateLimiterConfig = &rate_limit.DefaultRateLimitConfig{
	MaxTokens:  1200,
	RefillRate: 1000,
	OpCost:     1,
}
