package stats

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"time"
)

// QueryKey is one row of the per-day one_d4 query rollup (#1465): the
// bounded part of a query event. Every value is one_d4's own vocabulary
// (QueryEvent.java; otel_contract pins the two spellings together), and a
// word this build does not know collapses to "other" so the request is
// still counted — the same rule as an unknown method or agent — and drift
// shows up as an "other" row rather than as missing requests.
type QueryKey struct {
	Date    string
	Entry   string
	Source  string
	Outcome string
	Cache   string
}

// TermKey is one row of the per-day usage rollup of the query language:
// which fields, motifs, order-by motifs, and group-by columns queries used.
// All four are the compiler's vocabulary, bounded in one_d4 before the
// line is written.
type TermKey struct {
	Date  string
	Entry string
	Kind  string
	Term  string
}

const (
	KindField   = "field"
	KindMotif   = "motif"
	KindOrderBy = "order_by"
	KindGroupBy = "group_by"

	queryEventLogger  = "com.muchq.games.one_d4.query_event"
	queryEventMessage = "query_event"
	otherValue        = "other"
)

var (
	queryEntries  = map[string]bool{"query": true, "aggregate": true}
	querySources  = map[string]bool{"mcp": true, "ui": true, "api": true}
	queryOutcomes = map[string]bool{"ok": true, "invalid": true, "failed": true}
	queryCaches   = map[string]bool{"snapshot": true, "live": true, "none": true}
)

// queryEventLine is the slice of logback's JSON line this reads: the
// logger and message that mark a query event, and the key-value pairs,
// which logback renders as a list of one-entry objects with string values.
type queryEventLine struct {
	Timestamp  int64               `json:"timestamp"` // epoch millis, logback's JsonEncoder
	LoggerName string              `json:"loggerName"`
	Message    string              `json:"message"`
	KvpList    []map[string]string `json:"kvpList"`
}

// The day a line belongs to: its own timestamp, in UTC, or the object's
// date for a line without one.
func (l *queryEventLine) date(objectDate string) string {
	if l.Timestamp <= 0 {
		return objectDate
	}
	return time.UnixMilli(l.Timestamp).UTC().Format("2006-01-02")
}

func (l *queryEventLine) fields() map[string]string {
	out := make(map[string]string, len(l.KvpList))
	for _, pair := range l.KvpList {
		for key, value := range pair {
			out[key] = value
		}
	}
	return out
}

func known(vocabulary map[string]bool, value string) string {
	if vocabulary[value] {
		return value
	}
	return otherValue
}

// ConsumeQueryEvents aggregates one object's worth of one_d4 query-event
// lines into the rollup, each line under its own day, objectDate for a
// line without a timestamp. Lines that are not query events are skipped
// and counted — same contract as Consume.
func (r *Rollup) ConsumeQueryEvents(reader io.Reader, objectDate string) (skipped int, err error) {
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}
		var parsed queryEventLine
		if json.Unmarshal(line, &parsed) != nil ||
			parsed.LoggerName != queryEventLogger || parsed.Message != queryEventMessage {
			skipped++
			continue
		}
		date := parsed.date(objectDate)
		fields := parsed.fields()
		cache := fields["cache"]
		if cache == "" {
			cache = "none" // no cache decision was reached, as for an invalid query
		}
		key := QueryKey{
			Date:    date,
			Entry:   known(queryEntries, fields["entry"]),
			Source:  known(querySources, fields["source"]),
			Outcome: known(queryOutcomes, fields["outcome"]),
			Cache:   known(queryCaches, cache),
		}
		r.Queries[key]++

		r.addTerms(date, key.Entry, KindField, fields["fields"])
		r.addTerms(date, key.Entry, KindMotif, fields["motifs"])
		r.addTerms(date, key.Entry, KindOrderBy, fields["order_by"])
		r.addTerms(date, key.Entry, KindGroupBy, fields["group_by"])
	}
	if err := scanner.Err(); err != nil {
		return skipped, fmt.Errorf("reading log lines: %w", err)
	}
	return skipped, nil
}

func (r *Rollup) addTerms(date, entry, kind, joined string) {
	for _, term := range strings.Split(joined, ",") {
		if term != "" {
			r.Terms[TermKey{Date: date, Entry: entry, Kind: kind, Term: term}]++
		}
	}
}
