package stats

import (
	"fmt"
	"strings"
	"testing"
)

// Lines as logback's JsonEncoder writes them for one_d4's query event
// (LogbackConfigTest pins the shape): kvpList is a list of one-entry
// objects, every value a string.
func eventLine(pairs ...string) string {
	var kvps []string
	for i := 0; i+1 < len(pairs); i += 2 {
		kvps = append(kvps, fmt.Sprintf(`{"%s":"%s"}`, pairs[i], pairs[i+1]))
	}
	return `{"level":"INFO","loggerName":"com.muchq.games.one_d4.query_event","message":"query_event","kvpList":[` +
		strings.Join(kvps, ",") + `]}`
}

func TestConsumeQueryEventsRollsUpTheBoundedPartAndTheTerms(t *testing.T) {
	lines := strings.Join([]string{
		eventLine("entry", "query", "source", "ui", "fields", "white.elo,eco", "motifs", "fork",
			"order_by", "fork", "player", "false", "limit", "10", "offset", "0", "cache", "live",
			"rows", "3", "outcome", "ok", "duration_us", "1500"),
		eventLine("entry", "query", "source", "ui", "fields", "white.elo", "motifs", "",
			"order_by", "", "player", "true", "limit", "10", "offset", "0", "cache", "live",
			"rows", "0", "outcome", "ok", "duration_us", "500"),
		eventLine("entry", "query", "source", "mcp", "outcome", "invalid", "duration_us", "20"),
		eventLine("entry", "aggregate", "source", "api", "fields", "outcome", "motifs", "",
			"order_by", "", "player", "true", "group_by", "eco,opponent.elo(200)", "order", "score",
			"min_games", "5", "limit", "20", "rows", "2", "outcome", "ok", "duration_us", "9000"),
		// Not events: another logger, another message, garbage, a value off the vocabulary.
		`{"loggerName":"com.muchq.games.one_d4.api.ErrorHandler","message":"Unhandled","kvpList":[]}`,
		`{"loggerName":"com.muchq.games.one_d4.query_event","message":"something else","kvpList":[{"entry":"query"},{"source":"ui"},{"outcome":"ok"},{"duration_us":"1"}]}`,
		`not json`,
		eventLine("entry", "delete", "source", "ui", "outcome", "ok", "duration_us", "1"),
		eventLine("entry", "query", "source", "browser", "outcome", "ok", "duration_us", "1"),
	}, "\n")
	rollup := NewRollup()

	skipped, err := rollup.ConsumeQueryEvents(strings.NewReader(lines), "2026-09-01")

	if err != nil {
		t.Fatal(err)
	}
	if skipped != 5 {
		t.Errorf("skipped = %d, want the five non-events", skipped)
	}
	if got := rollup.Queries[QueryKey{"2026-09-01", "query", "ui", "ok", "live"}]; got != (QueryStat{2, 2000}) {
		t.Errorf("live ui queries = %+v, want 2 requests summing 2000us", got)
	}
	// An invalid query never reached the cache decision; its cache is "none".
	if got := rollup.Queries[QueryKey{"2026-09-01", "query", "mcp", "invalid", "none"}]; got != (QueryStat{1, 20}) {
		t.Errorf("invalid mcp query = %+v", got)
	}
	if got := rollup.Queries[QueryKey{"2026-09-01", "aggregate", "api", "ok", "none"}]; got != (QueryStat{1, 9000}) {
		t.Errorf("aggregate = %+v", got)
	}
	if len(rollup.Queries) != 3 {
		t.Errorf("query rows = %v; off-vocabulary lines must not mint rows", rollup.Queries)
	}

	want := map[TermKey]int64{
		{"2026-09-01", "query", KindField, "white.elo"}:               2,
		{"2026-09-01", "query", KindField, "eco"}:                     1,
		{"2026-09-01", "query", KindMotif, "fork"}:                    1,
		{"2026-09-01", "query", KindOrderBy, "fork"}:                  1,
		{"2026-09-01", "aggregate", KindField, "outcome"}:             1,
		{"2026-09-01", "aggregate", KindGroupBy, "eco"}:               1,
		{"2026-09-01", "aggregate", KindGroupBy, "opponent.elo(200)"}: 1,
	}
	if len(rollup.Terms) != len(want) {
		t.Errorf("terms = %v, want %v", rollup.Terms, want)
	}
	for key, count := range want {
		if rollup.Terms[key] != count {
			t.Errorf("term %+v = %d, want %d", key, rollup.Terms[key], count)
		}
	}
}

func TestConsumeQueryEventsCapsGroupByTermsPerObjectAndTheirLength(t *testing.T) {
	var lines strings.Builder
	for i := 0; i < maxGroupByTerms+50; i++ {
		lines.WriteString(eventLine("entry", "aggregate", "source", "api", "outcome", "ok",
			"duration_us", "1", "group_by", fmt.Sprintf("opponent.elo(%d)", i)) + "\n")
	}
	lines.WriteString(eventLine("entry", "query", "source", "api", "outcome", "ok", "duration_us", "1",
		"fields", strings.Repeat("x", 200)) + "\n")
	rollup := NewRollup()

	if _, err := rollup.ConsumeQueryEvents(strings.NewReader(lines.String()), "2026-09-01"); err != nil {
		t.Fatal(err)
	}

	groupBy := 0
	for key, count := range rollup.Terms {
		if key.Kind == KindGroupBy {
			groupBy++
			if key.Term == overflowTerm && count != 50 {
				t.Errorf("overflow row = %d, want the 50 past the cap", count)
			}
		}
		if len(key.Term) > maxTermLength {
			t.Errorf("term %q is longer than the cap", key.Term)
		}
	}
	if groupBy != maxGroupByTerms+1 {
		t.Errorf("distinct group-by terms = %d, want the cap plus the overflow row", groupBy)
	}
}
