package main

import (
	"github.com/hashicorp/golang-lru/v2/expirable"
	"github.com/muchq/moonbase/domains/platform/libs/clock"
	"github.com/muchq/moonbase/domains/platform/libs/mucks"
	"github.com/muchq/moonbase/domains/platform/libs/resilience4g/rate_limit"
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

// NewRouter wires the routes and middleware the service serves. Split out of
// main so tests drive the same object production does — the routing, the
// middleware order, and the rate-limit budgets are part of the wire contract,
// and a test that rebuilt them would be testing its own copy.
func NewRouter(api *ShortenerApi, fallback rate_limit.RateLimiterConfig,
	shorten rate_limit.RateLimiterConfig) http.Handler {
	router := mucks.NewMucks()

	// Add fallback rate-limiter at the router layer
	fallbackRateLimiter := MakeFallbackLimiterMiddleware(fallback)
	router.Add(fallbackRateLimiter)

	// Ping endpoint
	router.HandleFunc("GET /ping", PingHandler)

	// Rate-limited Shorten API endpoint
	shortenRateLimiter := MakeIpRateLimiterMiddleware(shorten)
	router.HandleFunc("POST /shorten",
		shortenRateLimiter.Wrap(api.ShortenHandler))

	// Non rate-limited Redirect API endpoint
	router.HandleFunc("GET /r/{slug}",
		api.RedirectHandler)

	return router
}

func main() {
	config := ReadConfig()

	shortenerApi := MakeShortenerApi(config)
	defer shortenerApi.Close()

	router := NewRouter(shortenerApi, FallbackRateLimiterConfig, ShortenRateLimiterConfig)

	log.Fatal(http.ListenAndServe(":"+config.Port, router))
}
