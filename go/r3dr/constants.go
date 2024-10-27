package main

import "github.com/muchq/moonbase/go/resilience4g/rate_limit"

const InternalError string = "internal error"
const NotFound string = "not found"

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
