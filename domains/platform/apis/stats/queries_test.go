package stats

import (
	"strings"
	"testing"
)

func TestConsumeQueryEventsRollsUpTheBoundedPartAndTheTerms(t *testing.T) {
	lines := strings.Join([]string{
		eventLine("entry", "query", "source", "ui", "fields", "white.elo,eco", "motifs", "fork",
			"order_by", "pin", "player", "false", "limit", "10", "offset", "0", "cache", "live",
			"rows", "3", "outcome", "ok", "duration_us", "1500"),
		eventLine("entry", "query", "source", "ui", "fields", "white.elo", "motifs", "",
			"order_by", "", "player", "true", "limit", "10", "offset", "0", "cache", "live",
			"rows", "0", "outcome", "ok", "duration_us", "500"),
		eventLine("entry", "query", "source", "mcp", "outcome", "invalid", "duration_us", "20"),
		eventLine("entry", "aggregate", "source", "api", "fields", "outcome", "motifs", "",
			"order_by", "", "player", "true", "group_by", "eco,opening_family", "order", "score",
			"min_games", "5", "limit", "20", "rows", "2", "outcome", "ok", "duration_us", "9000"),
		// Not events: another logger, another message, garbage.
		`{"loggerName":"com.muchq.games.one_d4.api.ErrorHandler","message":"Unhandled","kvpList":[]}`,
		`{"loggerName":"com.muchq.games.one_d4.query_event","message":"something else","kvpList":[{"entry":"query"},{"source":"ui"},{"outcome":"ok"},{"cache":"live"}]}`,
		`not json`,
	}, "\n")
	rollup := NewRollup()

	skipped, err := rollup.ConsumeQueryEvents(strings.NewReader(lines), "2026-09-01")

	if err != nil {
		t.Fatal(err)
	}
	if skipped != 3 {
		t.Errorf("skipped = %d, want the three non-events", skipped)
	}
	if got := rollup.Queries[QueryKey{"2026-09-01", "query", "ui", "ok", "live"}]; got != 2 {
		t.Errorf("live ui queries = %d, want 2", got)
	}
	// An invalid query never reached the cache decision; its cache is "none".
	if got := rollup.Queries[QueryKey{"2026-09-01", "query", "mcp", "invalid", "none"}]; got != 1 {
		t.Errorf("invalid mcp query = %d", got)
	}
	if got := rollup.Queries[QueryKey{"2026-09-01", "aggregate", "api", "ok", "none"}]; got != 1 {
		t.Errorf("aggregate = %d", got)
	}
	if len(rollup.Queries) != 3 {
		t.Errorf("query rows = %v", rollup.Queries)
	}

	want := map[TermKey]int64{
		{"2026-09-01", "query", KindField, "white.elo"}:            2,
		{"2026-09-01", "query", KindField, "eco"}:                  1,
		{"2026-09-01", "query", KindMotif, "fork"}:                 1,
		{"2026-09-01", "query", KindOrderBy, "pin"}:                1,
		{"2026-09-01", "aggregate", KindField, "outcome"}:          1,
		{"2026-09-01", "aggregate", KindGroupBy, "eco"}:            1,
		{"2026-09-01", "aggregate", KindGroupBy, "opening_family"}: 1,
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

// A word this build does not know is still a request: it collapses to
// "other" per column, so a one_d4 that learns a new outcome shows up as an
// "other" row rather than as fewer queries. Terms still count under the
// collapsed entry.
func TestConsumeQueryEventsCollapsesUnknownWordsToOtherAndKeepsCounting(t *testing.T) {
	lines := strings.Join([]string{
		eventLine("entry", "delete", "source", "ui", "outcome", "ok", "cache", "live", "fields", "eco"),
		eventLine("entry", "query", "source", "browser", "outcome", "ok", "cache", "live"),
		eventLine("entry", "query", "source", "ui", "outcome", "teapot", "cache", "live"),
		eventLine("entry", "query", "source", "ui", "outcome", "ok", "cache", "warm"),
	}, "\n")
	rollup := NewRollup()

	skipped, err := rollup.ConsumeQueryEvents(strings.NewReader(lines), "2026-09-01")

	if err != nil || skipped != 0 {
		t.Fatalf("ConsumeQueryEvents = (%d, %v); unknown words are counted, not skipped", skipped, err)
	}
	for _, key := range []QueryKey{
		{"2026-09-01", "other", "ui", "ok", "live"},
		{"2026-09-01", "query", "other", "ok", "live"},
		{"2026-09-01", "query", "ui", "other", "live"},
		{"2026-09-01", "query", "ui", "ok", "other"},
	} {
		if rollup.Queries[key] != 1 {
			t.Errorf("row %+v = %d, want 1", key, rollup.Queries[key])
		}
	}
	if len(rollup.Queries) != 4 {
		t.Errorf("query rows = %v", rollup.Queries)
	}
	if rollup.Terms[TermKey{"2026-09-01", "other", KindField, "eco"}] != 1 {
		t.Errorf("terms = %v; the collapsed entry still carries its terms", rollup.Terms)
	}
}

// Logback names a roll for the hour it covers, so the object date is
// usually right; the line's own timestamp (epoch millis) wins anyway,
// and a line without one takes the object's.
func TestConsumeQueryEventsDatesEachLineByItsOwnTimestamp(t *testing.T) {
	stamped := `{"timestamp":1788220800500,"level":"INFO","loggerName":"com.muchq.games.one_d4.query_event","message":"query_event","kvpList":[{"entry":"query"},{"source":"ui"},{"outcome":"ok"},{"cache":"live"}]}`
	unstamped := eventLine("entry", "query", "source", "ui", "outcome", "ok", "cache", "live")
	rollup := NewRollup()
	if _, err := rollup.ConsumeQueryEvents(strings.NewReader(stamped+"\n"+unstamped+"\n"), "2026-09-03"); err != nil {
		t.Fatal(err)
	}
	dates := map[string]int64{}
	for key, count := range rollup.Queries {
		dates[key.Date] += count
	}
	if dates["2026-09-01"] != 1 || dates["2026-09-03"] != 1 || len(dates) != 2 {
		t.Errorf("queries by date = %v, want one on 2026-09-01 and one on 2026-09-03", dates)
	}
}
