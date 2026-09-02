package stats

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"strconv"
	"strings"
)

// QueryKey is one row of the per-day one_d4 query rollup (#1465): the
// bounded part of a query event, every value from one_d4's own vocabulary
// (QueryEvent.java) and checked against it here, so a line from a future
// one_d4 that speaks a word this build does not know is skipped rather
// than minting a row.
type QueryKey struct {
	Date    string
	Entry   string
	Source  string
	Outcome string
	Cache   string
}

// QueryStat is what a QueryKey accumulates: how many, and their summed
// handler time, so a window's mean is a division at read time.
type QueryStat struct {
	Requests   int64
	DurationUs int64
}

// TermKey is one row of the per-day usage rollup of the query language:
// which fields, motifs, order-by motifs, and group-by terms queries used.
// Fields and motifs are the compiler's vocabulary, bounded in one_d4;
// group-by terms carry a caller-chosen bucket width, so they are capped
// per object like agent names are.
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
	maxTermLength     = 64
	maxGroupByTerms   = 200
	overflowTerm      = "(more)"
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
	LoggerName string              `json:"loggerName"`
	Message    string              `json:"message"`
	KvpList    []map[string]string `json:"kvpList"`
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

// ConsumeQueryEvents aggregates one object's worth of one_d4 query-event
// lines into the rollup under the given partition date. Lines that are not
// query events, or that use a value outside the vocabulary above, are
// skipped and counted — same contract as Consume.
func (r *Rollup) ConsumeQueryEvents(reader io.Reader, date string) (skipped int, err error) {
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
		fields := parsed.fields()
		key := QueryKey{
			Date:    date,
			Entry:   fields["entry"],
			Source:  fields["source"],
			Outcome: fields["outcome"],
			Cache:   fields["cache"],
		}
		if key.Cache == "" {
			key.Cache = "none"
		}
		if !queryEntries[key.Entry] || !querySources[key.Source] ||
			!queryOutcomes[key.Outcome] || !queryCaches[key.Cache] {
			skipped++
			continue
		}
		durationUs, _ := strconv.ParseInt(fields["duration_us"], 10, 64)
		stat := r.Queries[key]
		stat.Requests++
		stat.DurationUs += durationUs
		r.Queries[key] = stat

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
		if term == "" {
			continue
		}
		if len(term) > maxTermLength {
			term = term[:maxTermLength]
		}
		if kind == KindGroupBy {
			term = r.boundedGroupBy(term)
		}
		r.Terms[TermKey{Date: date, Entry: entry, Kind: kind, Term: term}]++
	}
}

// boundedGroupBy applies the per-object cap to group-by terms, the one kind
// whose spelling the caller partly chooses.
func (r *Rollup) boundedGroupBy(term string) string {
	if r.groupByTerms[term] {
		return term
	}
	if len(r.groupByTerms) >= maxGroupByTerms {
		return overflowTerm
	}
	r.groupByTerms[term] = true
	return term
}
