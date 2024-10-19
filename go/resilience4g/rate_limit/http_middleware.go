package rate_limit

import (
	"github.com/muchq/moonbase/go/mucks"
	"net"
	"net/http"
	"sync"
)

type KeyExtractor interface {
	Apply(r *http.Request) string
}

type RemoteIpKeyExtractor struct {
}

// RemoteIpKeyExtractor tries to read the request's remote-ip
// from the X-Forwarded-For header. If that header is not present,
// we fall back to the RemoteAddr of the request.
// Note that X-Forwarded-For should be populated by the LB and
// RemoteAddr is only a good fallback in local testing.
func (RemoteIpKeyExtractor) Apply(r *http.Request) string {
	ip := r.Header.Get("X-Forwarded-For")
	if ip == "" {
		ip, _, _ = net.SplitHostPort(r.RemoteAddr)
	}
	return ip
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

		m.ensureLimiter(key)
		limiter := m.Limiters[key]
		if limiter.Allow(m.Config.OpCost) {
			next(w, r)
		} else {
			http.Error(w, "rate limit exceeded", http.StatusTooManyRequests)
		}
	}
}

func (m *RateLimiterMiddleware) ensureLimiter(key string) {
	m.Mutex.Lock()
	defer m.Mutex.Unlock()
	_, ok := m.Limiters[key]
	if !ok {
		m.Limiters[key] = m.Factory.NewRateLimiter(m.Config)
	}
}
