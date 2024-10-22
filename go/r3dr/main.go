package main

import (
	"github.com/muchq/moonbase/go/mucks"
	"github.com/muchq/moonbase/go/resilience4g/rate_limit"
	"log"
	"net/http"
)

func MakeShortenerApi(config Config) *ShortenerApi {
	shortDB := NewShortDB(config)
	shortener := NewShortener(shortDB)
	return NewShortenerApi(shortener)
}

func MakeIpRateLimiterMiddleware() mucks.Middleware {
	return rate_limit.NewRateLimiterMiddleware(
		rate_limit.TokenBucketRateLimiterFactory{},
		rate_limit.RemoteIpKeyExtractor{},
		ShortenRateLimiterConfig)
}

func main() {
	config := ReadConfig()

	shortenerApi := MakeShortenerApi(config)
	defer shortenerApi.Close()

	router := mucks.NewMucks()
	router.HandleFunc("GET /ping", PingHandler)

	// Rate-limited Shorten API endpoint
	ipRateLimiter := MakeIpRateLimiterMiddleware()
	router.HandleFunc("POST /shorten",
		ipRateLimiter.Wrap(shortenerApi.ShortenHandler))

	// Non rate-limited Redirect API endpoint
	router.HandleFunc("GET /r/{slug}",
		shortenerApi.RedirectHandler)

	log.Fatal(http.ListenAndServe(":"+config.Port, router))
}
