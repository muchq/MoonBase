// Characterization of r3dr's HTTP wire contract, pinned before the smithy-cpp
// rewrite (MoonBase#1359, phase 2). These tests describe what the Go service
// does today — including the parts we intend to change — so the port has a
// spec to differ from deliberately rather than by accident.
//
// They drive the production router (main.go:NewRouter) over httptest and read
// the raw response, so a change to routing, middleware order, status codes,
// headers, or body shape fails here. Where today's behavior is a defect, the
// test says so in a comment and pins it anyway; changing it is a decision for
// the rewrite, and this suite is what will show the diff.
package main

import (
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/hashicorp/golang-lru/v2/expirable"
	"github.com/muchq/moonbase/domains/platform/libs/clock"
	"github.com/muchq/moonbase/domains/platform/libs/resilience4g/rate_limit"
	"github.com/stretchr/testify/assert"
)

// A slug table with no failure modes, so a wire assertion that goes wrong is
// about the wire and not about a fake running out of budget.
type wireDao struct {
	slug    string
	targets map[string]string
	// inserted and reads record what the handler passed down, so the tests
	// can assert on values that never appear in the response.
	inserted []insertedUrl
	reads    []string
}

type insertedUrl struct {
	longUrl   string
	expiresAt int64
}

func (d *wireDao) InsertUrl(longUrl string, expiresAt int64) (string, error) {
	d.inserted = append(d.inserted, insertedUrl{longUrl: longUrl, expiresAt: expiresAt})
	return d.slug, nil
}

func (d *wireDao) GetLongUrl(slug string) (string, error) {
	d.reads = append(d.reads, slug)
	// The real ShortDB returns ("", nil) for a slug that is not in the table.
	return d.targets[slug], nil
}

func (*wireDao) Close() {}

// Budgets big enough that the limiter never fires; the tests that care about
// rate limiting pass the production constants instead.
var generousLimit = &rate_limit.DefaultRateLimitConfig{MaxTokens: 10000, RefillRate: 10000, OpCost: 1}

func newWireServer(t *testing.T, dao *wireDao,
	shortenLimit rate_limit.RateLimiterConfig) http.Handler {
	t.Helper()
	cache := expirable.NewLRU[string, string](CacheConfig.MaxItems, nil,
		time.Minute*CacheConfig.ExpirationMinutes)
	api := NewShortenerApi(clock.NewSystemUtcClock(), NewShortener(dao, cache))
	return NewRouter(api, generousLimit, shortenLimit)
}

func do(t *testing.T, handler http.Handler, method, target, body string) *httptest.ResponseRecorder {
	t.Helper()
	var reader io.Reader
	if body != "" {
		reader = strings.NewReader(body)
	}
	request := httptest.NewRequest(method, target, reader)
	recorder := httptest.NewRecorder()
	handler.ServeHTTP(recorder, request)
	return recorder
}

func futureMillis(d time.Duration) int64 {
	return time.Now().Add(d).UnixMilli()
}

// decodeProblem reads a mucks.Problem body. `instance` is a fresh UUID per
// response, so it is asserted as non-empty rather than compared — a fact the
// port has to reckon with, since smithy errors carry x-correlation-id instead.
func decodeProblem(t *testing.T, recorder *httptest.ResponseRecorder) map[string]any {
	t.Helper()
	var problem map[string]any
	assert.NoError(t, json.Unmarshal(recorder.Body.Bytes(), &problem))
	assert.NotEmpty(t, problem["instance"], "every Problem carries a fresh uuid")
	return problem
}

func TestShortenReturns201WithSlugBody(t *testing.T) {
	dao := &wireDao{slug: "AQA"}
	handler := newWireServer(t, dao, generousLimit)

	recorder := do(t, handler, "POST", "/shorten",
		`{"longUrl":"https://www.google.com","expiresAt":`+
			itoa(futureMillis(24*time.Hour))+`}`)

	assert.Equal(t, http.StatusCreated, recorder.Code)
	assert.Equal(t, "application/json; charset=utf-8", recorder.Header().Get("Content-Type"))
	// The success body is exactly one key. json.Encoder appends a newline.
	assert.Equal(t, "{\"slug\":\"AQA\"}\n", recorder.Body.String())
}

func TestShortenPassesTheRequestedExpiryToTheStore(t *testing.T) {
	dao := &wireDao{slug: "AQA"}
	handler := newWireServer(t, dao, generousLimit)
	expiry := futureMillis(24 * time.Hour)

	do(t, handler, "POST", "/shorten",
		`{"longUrl":"https://www.google.com","expiresAt":`+itoa(expiry)+`}`)

	// The value reaches the store. Whether the store ever reads it back is a
	// different question — see TestTheReadPathCannotFilterOnExpiry.
	assert.Equal(t, []insertedUrl{{longUrl: "https://www.google.com", expiresAt: expiry}},
		dao.inserted)
}

// Every rejection the service makes, at the wire. The detail strings are part
// of the contract the web client never reads but a future client might, and
// they are the checklist the Smithy model's constraint traits have to cover.
func TestShortenRejections(t *testing.T) {
	past := time.Now().Add(-time.Hour).UnixMilli()
	tooFar := futureMillis(32 * 24 * time.Hour)
	valid := itoa(futureMillis(24 * time.Hour))

	cases := []struct {
		name   string
		body   string
		detail string
	}{
		{"empty long url", `{"longUrl":"","expiresAt":` + valid + `}`, "longUrl is required"},
		{"no protocol", `{"longUrl":"google.com","expiresAt":` + valid + `}`,
			"longUrl must include protocol"},
		{"too short", `{"longUrl":"http://g.c","expiresAt":` + valid + `}`, "longUrl too short"},
		{"too long", `{"longUrl":"http://g.co?q=` + strings.Repeat("a", 1000) + `","expiresAt":` +
			valid + `}`, "max url length is 1000 chars"},
		{"expiry in the past", `{"longUrl":"https://g.co","expiresAt":` + itoa(past) + `}`,
			"expiration time is in the past"},
		{"expiry too far out", `{"longUrl":"https://g.co","expiresAt":` + itoa(tooFar) + `}`,
			"max URL lifetime is 30 days"},
		// A missing expiresAt is NOT accepted, though apis/r3dr/README.md says
		// "expiresAt is optional": the zero value decodes as epoch 0, which is
		// in the past. The README is wrong, and the web client hides it by
		// always sending now+7d.
		{"missing expiry", `{"longUrl":"https://www.google.com"}`,
			"expiration time is in the past"},
	}

	for _, testCase := range cases {
		t.Run(testCase.name, func(t *testing.T) {
			handler := newWireServer(t, &wireDao{slug: "AQA"}, generousLimit)

			recorder := do(t, handler, "POST", "/shorten", testCase.body)

			assert.Equal(t, http.StatusBadRequest, recorder.Code)
			assert.Equal(t, "application/json; charset=utf-8",
				recorder.Header().Get("Content-Type"))
			problem := decodeProblem(t, recorder)
			assert.Equal(t, float64(400), problem["status"])
			assert.Equal(t, float64(400), problem["errorCode"])
			assert.Equal(t, "Bad Request", problem["message"])
			assert.Equal(t, testCase.detail, problem["detail"])
		})
	}
}

// The boundary the message misnames: dto.go allows 31 days while telling the
// caller the limit is 30. Pinned on both sides so the rewrite picks a number
// on purpose.
func TestShortenExpiryWindowIsThirtyOneDaysDespiteTheMessage(t *testing.T) {
	handler := newWireServer(t, &wireDao{slug: "AQA"}, generousLimit)

	justInside := do(t, handler, "POST", "/shorten",
		`{"longUrl":"https://www.google.com","expiresAt":`+
			itoa(futureMillis(30*24*time.Hour+23*time.Hour))+`}`)
	assert.Equal(t, http.StatusCreated, justInside.Code, "30d23h is accepted")

	justOutside := do(t, handler, "POST", "/shorten",
		`{"longUrl":"https://www.google.com","expiresAt":`+
			itoa(futureMillis(31*24*time.Hour+time.Hour))+`}`)
	assert.Equal(t, http.StatusBadRequest, justOutside.Code, "31d1h is rejected")
	assert.Equal(t, "max URL lifetime is 30 days", decodeProblem(t, justOutside)["detail"])
}

func TestShortenRejectsMalformedJson(t *testing.T) {
	handler := newWireServer(t, &wireDao{slug: "AQA"}, generousLimit)

	recorder := do(t, handler, "POST", "/shorten", `{"longUrl":`)

	assert.Equal(t, http.StatusBadRequest, recorder.Code)
	// The decoder's own message reaches the caller as the Problem detail.
	assert.Equal(t, "unexpected EOF", decodeProblem(t, recorder)["detail"])
}

func TestRedirectReturns302WithLocation(t *testing.T) {
	dao := &wireDao{targets: map[string]string{"AQA": "https://www.google.com"}}
	handler := newWireServer(t, dao, generousLimit)

	recorder := do(t, handler, "GET", "/r/AQA", "")

	assert.Equal(t, http.StatusFound, recorder.Code)
	assert.Equal(t, "https://www.google.com", recorder.Header().Get("Location"))
}

// An unknown slug answers Go's stdlib 404 — text/plain, not the JSON Problem
// every other error uses. Two error shapes on one service; the port collapses
// them, so the difference is pinned here first.
func TestRedirectUnknownSlugReturnsPlainText404(t *testing.T) {
	handler := newWireServer(t, &wireDao{targets: map[string]string{}}, generousLimit)

	recorder := do(t, handler, "GET", "/r/nope", "")

	assert.Equal(t, http.StatusNotFound, recorder.Code)
	assert.Equal(t, "text/plain; charset=utf-8", recorder.Header().Get("Content-Type"))
	assert.Equal(t, "404 page not found\n", recorder.Body.String())
}

// A one-character slug never reaches the store: Shortener.Redirect rejects it
// before the lookup, and the handler maps that to the same 404.
func TestRedirectShortSlugIs404(t *testing.T) {
	handler := newWireServer(t, &wireDao{targets: map[string]string{"a": "https://g.co"}},
		generousLimit)

	recorder := do(t, handler, "GET", "/r/a", "")

	assert.Equal(t, http.StatusNotFound, recorder.Code)
}

// The defect worth the most attention in the rewrite (MoonBase#1359, finding
// 1): expiry is written and never read.
//
// What this test can prove is that the read path carries no expiry at all —
// the store is asked for a slug and nothing else, no clock reaches it, and
// whatever it returns is handed to the client unfiltered. So no expires_at
// value, however old, can turn this lookup into a 404 above the SQL. That the
// SQL has no predicate either is a fact about ShortDB, and gets its own test
// against real postgres when the C++ store lands (phase 3) — a fake cannot
// stand in for it, because a fake that ignored expiry would look identical.
//
// Pinned so enforcing expiry shows up as a deliberate diff, not a silent one.
func TestTheReadPathCannotFilterOnExpiry(t *testing.T) {
	dao := &wireDao{slug: "AQA", targets: map[string]string{"AQA": "https://www.google.com"}}
	handler := newWireServer(t, dao, generousLimit)

	created := do(t, handler, "POST", "/shorten",
		`{"longUrl":"https://www.google.com","expiresAt":`+
			itoa(futureMillis(time.Minute))+`}`)
	assert.Equal(t, http.StatusCreated, created.Code)
	assert.Len(t, dao.inserted, 1, "the expiry was recorded on the way in")

	recorder := do(t, handler, "GET", "/r/AQA", "")

	assert.Equal(t, http.StatusFound, recorder.Code)
	assert.Equal(t, "https://www.google.com", recorder.Header().Get("Location"))
	// The whole of what the read path asked for. No expiry, no clock: the
	// UrlDao interface has no parameter that could carry one.
	assert.Equal(t, []string{"AQA"}, dao.reads)
}

func TestPingReturnsPlainTextPong(t *testing.T) {
	handler := newWireServer(t, &wireDao{}, generousLimit)

	recorder := do(t, handler, "GET", "/ping", "")

	assert.Equal(t, http.StatusOK, recorder.Code)
	assert.Equal(t, "pong", recorder.Body.String())
}

// Unrouted paths get the JSON Problem 404 from mucks, unlike the redirect
// miss above. Both are 404s with different bodies.
func TestUnroutedPathReturnsJsonProblem404(t *testing.T) {
	handler := newWireServer(t, &wireDao{}, generousLimit)

	recorder := do(t, handler, "GET", "/nope", "")

	assert.Equal(t, http.StatusNotFound, recorder.Code)
	assert.Equal(t, "application/json; charset=utf-8", recorder.Header().Get("Content-Type"))
	problem := decodeProblem(t, recorder)
	assert.Equal(t, "Not Found", problem["message"])
}

// The shorten budget with the production constants: two tokens, refilling at
// one per second, keyed per client. The third immediate request is refused.
func TestShortenRateLimitReturns429WithProblemBody(t *testing.T) {
	handler := newWireServer(t, &wireDao{slug: "AQA"}, ShortenRateLimiterConfig)
	body := `{"longUrl":"https://www.google.com","expiresAt":` +
		itoa(futureMillis(24*time.Hour)) + `}`

	first := do(t, handler, "POST", "/shorten", body)
	second := do(t, handler, "POST", "/shorten", body)
	third := do(t, handler, "POST", "/shorten", body)

	assert.Equal(t, http.StatusCreated, first.Code)
	assert.Equal(t, http.StatusCreated, second.Code)
	assert.Equal(t, http.StatusTooManyRequests, third.Code)

	problem := decodeProblem(t, third)
	assert.Equal(t, float64(429), problem["status"])
	assert.Equal(t, "Rate Limit Exceeded", problem["message"])
	// No Retry-After today. aura's limiter sends one, so the port gains a
	// header here rather than losing one.
	assert.Empty(t, third.Header().Get("Retry-After"))
}

// Redirects carry no per-client budget at all — only the router-wide fallback
// bucket. Well past the shorten limit of two, every redirect still succeeds.
func TestRedirectIsNotPerClientRateLimited(t *testing.T) {
	dao := &wireDao{targets: map[string]string{"AQA": "https://www.google.com"}}
	handler := newWireServer(t, dao, ShortenRateLimiterConfig)

	for i := 0; i < 10; i++ {
		recorder := do(t, handler, "GET", "/r/AQA", "")
		assert.Equal(t, http.StatusFound, recorder.Code, "redirect %d", i)
	}
}

func itoa(v int64) string {
	return strconv.FormatInt(v, 10)
}
