package main

import (
	"github.com/hashicorp/golang-lru/v2/expirable"
	"github.com/muchq/moonbase/go/clock"
	"github.com/muchq/moonbase/go/mucks"
	"github.com/muchq/moonbase/go/resilience4g/rate_limit"
	"log"
	"net/http"
	"time"
)

func MakeShortenerApi(config Config) *ShortenerApi {
	shortDB := NewShortDB(config)
	cacheConfig := config.CacheConfig
	cache := expirable.NewLRU[string, string](cacheConfig.MaxItems, nil, time.Minute*cacheConfig.ExpirationMinutes)
	shortener := NewShortener(shortDB, cache)
	return NewShortenerApi(clock.NewSystemUtcClock(), shortener)
}

func MakeFallbackLimiterMiddleware(config rate_limit.RateLimiterConfig) mucks.Middleware {
	return rate_limit.NewRateLimiterMiddleware(
		rate_limit.TokenBucketRateLimiterFactory{},
		rate_limit.ConstKeyExtractor{},
		config)
}

func MakeIpRateLimiterMiddleware(config rate_limit.RateLimiterConfig) mucks.Middleware {
	return rate_limit.NewRateLimiterMiddleware(
		rate_limit.TokenBucketRateLimiterFactory{},
		rate_limit.RemoteIpKeyExtractor{},
		config)
}

func main() {
	config := ReadConfig()

	shortenerApi := MakeShortenerApi(config)
	defer shortenerApi.Close()

	router := mucks.NewMucks()

	// Add fallback rate-limiter at the router layer
	fallbackRateLimiter := MakeFallbackLimiterMiddleware(FallbackRateLimiterConfig)
	router.Add(fallbackRateLimiter)

	// Ping endpoint
	router.HandleFunc("GET /ping", PingHandler)

	// Rate-limited Shorten API endpoint
	shortenRateLimiter := MakeIpRateLimiterMiddleware(ShortenRateLimiterConfig)
	router.HandleFunc("POST /shorten",
		shortenRateLimiter.Wrap(shortenerApi.ShortenHandler))

	// Non rate-limited Redirect API endpoint
	router.HandleFunc("GET /r/{slug}",
		shortenerApi.RedirectHandler)

	log.Fatal(http.ListenAndServe(":"+config.Port, router))
}
