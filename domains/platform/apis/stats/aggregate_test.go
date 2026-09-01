package stats

import (
	"strings"
	"testing"
)

const sampleLines = `{"status":200,"request":{"host":"api.1d4.net","method":"POST","uri":"/mcp","headers":{"User-Agent":["Mozilla/5.0 (Macintosh) Chrome/126.0"]}}}
{"status":200,"request":{"host":"api.1d4.net","method":"POST","uri":"/mcp","headers":{"User-Agent":["Mozilla/5.0 (Macintosh) Chrome/126.0"]}}}
{"status":403,"request":{"host":"git.muchq.com","method":"GET","uri":"/repo/src","headers":{"User-Agent":["meta-externalagent/1.1"]}}}
{"status":302,"request":{"host":"i.iili.uk","method":"GET","uri":"/r/abc123","headers":{"User-Agent":["curl/8.6.0"]}}}
{"status":302,"request":{"host":"i.iili.uk","method":"GET","uri":"/r/abc123","headers":{"User-Agent":["Mozilla/5.0 Chrome"]}}}
{"status":404,"request":{"host":"i.iili.uk","method":"GET","uri":"/r/gone","headers":{}}}
not json at all
{"status":418,"request":{"method":"GET","uri":"/hostless","headers":{}}}
{"status":200,"request":{"host":"api.muchq.com","method":"WEIRD","uri":"/x","headers":{}}}
`

func TestConsumeAggregatesRequestsSlugsAndSkipsCorruptLines(t *testing.T) {
	rollup := NewRollup()

	skipped, err := rollup.Consume(strings.NewReader(sampleLines), "2026-08-30")

	if err != nil {
		t.Fatal(err)
	}
	// The bare-garbage line and the hostless line are skipped, counted.
	if skipped != 2 {
		t.Errorf("skipped = %d, want 2", skipped)
	}
	if got := rollup.Requests[RequestKey{"2026-08-30", "api.1d4.net", 200, "POST", AgentBrowser}]; got != 2 {
		t.Errorf("mcp browser POSTs = %d, want 2", got)
	}
	if got := rollup.Requests[RequestKey{"2026-08-30", "git.muchq.com", 403, "GET", AgentAIScraper}]; got != 1 {
		t.Errorf("blocked ai scraper = %d, want 1", got)
	}
	// An invented verb collapses like every metrics rail's method label.
	if got := rollup.Requests[RequestKey{"2026-08-30", "api.muchq.com", 200, "CUSTOM", AgentOther}]; got != 1 {
		t.Errorf("CUSTOM-method row = %d, want 1", got)
	}
	// The redirect rollup counts per slug and status, across agent classes.
	if got := rollup.Slugs[SlugKey{"2026-08-30", "abc123", 302}]; got != 2 {
		t.Errorf("abc123 hits = %d, want 2", got)
	}
	if got := rollup.Slugs[SlugKey{"2026-08-30", "gone", 404}]; got != 1 {
		t.Errorf("gone-slug 404s = %d, want 1", got)
	}
}

func TestConsumeSurvivesOversizedLines(t *testing.T) {
	huge := `{"status":200,"request":{"host":"x","method":"GET","uri":"/` +
		strings.Repeat("a", 2*1024*1024) + `","headers":{}}}`

	_, err := NewRollup().Consume(strings.NewReader(huge), "2026-08-30")

	// A line over the scanner cap is an error the caller must see — the
	// object needs investigating — not a silent partial read.
	if err == nil {
		t.Error("a line over the scanner's cap must surface as an error")
	}
}
