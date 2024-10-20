package main

import "github.com/muchq/moonbase/go/resilience4g/rate_limit"

const InternalError string = "internal error"
const NotFound string = "not found"

var ShortenRateLimiterConfig = rate_limit.RateLimiterConfig{
	MaxTokens:  2,
	RefillRate: 1,
	OpCost:     1,
}
