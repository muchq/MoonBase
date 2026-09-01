package stats

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
)

// RequestKey is one row of the per-day request rollup: everything bounded,
// nothing caller-controlled — host is one of Caddy's configured vhosts,
// the method collapses through the same nine-verb rule the metrics rails
// use, and the agent class is the four-value vocabulary in classify.go.
type RequestKey struct {
	Date       string
	Host       string
	Status     int
	Method     string
	AgentClass string
}

// SlugKey is one row of the iili redirect rollup. The slug is
// caller-shaped but bounded by SlugOf (one path segment, max 64 bytes),
// and only rows for requests that reached the redirect route exist at all.
type SlugKey struct {
	Date   string
	Slug   string
	Status int
}

// Rollup is one processed object's aggregates, accumulated in memory and
// applied to the store in a single transaction with the processed marker —
// so a crash between the two reprocesses the object rather than losing or
// double-counting it.
type Rollup struct {
	Requests map[RequestKey]int64
	Slugs    map[SlugKey]int64
}

func NewRollup() *Rollup {
	return &Rollup{Requests: map[RequestKey]int64{}, Slugs: map[SlugKey]int64{}}
}

// caddyLine is the slice of Caddy's JSON access log this pipeline reads.
// Everything else in the line is ignored on decode.
type caddyLine struct {
	Status  int `json:"status"`
	Request struct {
		Host    string              `json:"host"`
		Method  string              `json:"method"`
		URI     string              `json:"uri"`
		Headers map[string][]string `json:"headers"`
	} `json:"request"`
}

func (l *caddyLine) userAgent() string {
	values := l.Request.Headers["User-Agent"]
	if len(values) == 0 {
		return ""
	}
	return values[0]
}

// The nine RFC 9110 methods pass through, anything else collapses —
// the same bounding rule as every metrics rail (#1305), because a scanner
// spraying invented verbs must not mint a row per token.
var knownMethods = map[string]bool{
	"GET": true, "HEAD": true, "POST": true, "PUT": true, "DELETE": true,
	"CONNECT": true, "OPTIONS": true, "TRACE": true, "PATCH": true,
}

func boundedMethod(method string) string {
	if knownMethods[method] {
		return method
	}
	return "CUSTOM"
}

// Consume aggregates one object's worth of Caddy JSON lines into the
// rollup. date is the object's dt= partition — the roll date — not
// anything parsed out of the lines. Unparseable lines are counted and
// skipped: one corrupt line must not discard the other hundred thousand,
// but a wholly corrupt object should be loud, so the count comes back.
func (r *Rollup) Consume(reader io.Reader, date string) (skipped int, err error) {
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}
		var parsed caddyLine
		if json.Unmarshal(line, &parsed) != nil || parsed.Request.Host == "" {
			skipped++
			continue
		}
		method := boundedMethod(parsed.Request.Method)
		r.Requests[RequestKey{
			Date:       date,
			Host:       parsed.Request.Host,
			Status:     parsed.Status,
			Method:     method,
			AgentClass: AgentClassOf(parsed.userAgent()),
		}]++
		if slug := SlugOf(parsed.Request.Host, parsed.Request.Method, parsed.Request.URI); slug != "" {
			r.Slugs[SlugKey{Date: date, Slug: slug, Status: parsed.Status}]++
		}
	}
	if err := scanner.Err(); err != nil {
		return skipped, fmt.Errorf("reading log lines: %w", err)
	}
	return skipped, nil
}
