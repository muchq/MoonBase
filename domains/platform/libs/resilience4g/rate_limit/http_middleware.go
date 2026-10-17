package rate_limit

import (
	"fmt"
	"github.com/google/uuid"
	"github.com/muchq/moonbase/domains/platform/libs/mucks"
	"net"
	"net/http"
	"net/netip"
	"sync"
)

type KeyExtractor interface {
	Apply(r *http.Request) string
}

type ConstKeyExtractor struct {
}

// Apply implements the KeyExtractor interface.
// ConstKeyExtractor always returns the same key.
func (ConstKeyExtractor) Apply(r *http.Request) string {
	return "SENTINEL"
}

type PeerIpKeyExtractor struct {
}

// Apply implements the KeyExtractor interface.
// PeerIpKeyExtractor keys on the socket peer in canonical form (mapped v6
// as v4, no zone), or "" when RemoteAddr holds no address. It never reads
// X-Forwarded-For: the header starts life client-authored, so without a
// trust boundary (opal-cpp ADR-0012, as aura and server_pal implement it)
// a key read from it is the client's to choose. Behind a reverse proxy
// every request therefore shares the proxy's key.
func (PeerIpKeyExtractor) Apply(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return ""
	}
	addr, err := netip.ParseAddr(host)
	if err != nil {
		return ""
	}
	return addr.Unmap().WithZone("").String()
}

// RateLimiterMiddleware implements mucks.Middleware
type RateLimiterMiddleware struct {
	Factory   RateLimiterFactory
	Limiters  map[string]RateLimiterInterface
	Extractor KeyExtractor
	Config    RateLimiterConfig
	Mutex     sync.Mutex
}

func NewRateLimiterMiddleware(factory RateLimiterFactory, extractor KeyExtractor, config RateLimiterConfig) mucks.Middleware {
	return &RateLimiterMiddleware{
		Factory:   factory,
		Limiters:  make(map[string]RateLimiterInterface),
		Extractor: extractor,
		Config:    config,
	}
}

func (m *RateLimiterMiddleware) Wrap(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		key := m.Extractor.Apply(r)

		// TODO: what should happen if the extracted key is empty?

		limiter, err := m.limiterFor(key)
		if err != nil {
			// TODO: throttle logging
			fmt.Println("failing open due to error creating rate limiter: ", err)
			next(w, r)
			return
		}

		if limiter.Allow(m.Config.GetOpCost()) {
			next(w, r)
		} else {
			mucks.JsonError(w, NewRateLimitExceeded())
		}
	}
}

func (m *RateLimiterMiddleware) limiterFor(key string) (RateLimiterInterface, error) {
	m.Mutex.Lock()
	defer m.Mutex.Unlock()
	if limiter, ok := m.Limiters[key]; ok {
		return limiter, nil
	}
	limiter, err := m.Factory.NewRateLimiter(m.Config)
	if err != nil {
		return nil, err
	}
	m.Limiters[key] = limiter
	return limiter, nil
}

func NewRateLimitExceeded() mucks.Problem {
	return mucks.Problem{
		StatusCode: http.StatusTooManyRequests,
		ErrorCode:  http.StatusTooManyRequests,
		Message:    "Rate Limit Exceeded",
		Detail:     "Rate Limit Exceeded", // retry-after etc
		Instance:   uuid.NewString(),
	}
}
