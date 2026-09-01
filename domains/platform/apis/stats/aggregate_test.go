package stats

import (
	"fmt"
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
{"status":404,"request":{"host":"api.muchq.com","method":"GET","uri":"/wp-login.php","headers":{"User-Agent":["python-requests/2.32.0"]}}}
{"status":404,"request":{"host":"api.muchq.com","method":"GET","uri":"/.env","headers":{"User-Agent":["Mozilla/5.0 (compatible; GPTBot/1.2)"]}}}
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
	// Browsers are one unnamed bucket; every other row carries its agent's
	// bounded name, so "did meta back off after the 403" is a query over the
	// same rows the class totals come from rather than a re-aggregation.
	if got := rollup.Requests[RequestKey{"2026-08-30", "api.1d4.net", 200, "POST", AgentBrowser, ""}]; got != 2 {
		t.Errorf("mcp browser POSTs = %d, want 2", got)
	}
	if got := rollup.Requests[RequestKey{"2026-08-30", "git.muchq.com", 403, "GET", AgentAIScraper, "meta-externalagent"}]; got != 1 {
		t.Errorf("blocked ai scraper = %d, want 1", got)
	}
	if got := rollup.Requests[RequestKey{"2026-08-30", "i.iili.uk", 302, "GET", AgentBot, "curl"}]; got != 1 {
		t.Errorf("curl redirects = %d, want 1", got)
	}
	if got := rollup.Requests[RequestKey{"2026-08-30", "i.iili.uk", 404, "GET", AgentOther, "(empty)"}]; got != 1 {
		t.Errorf("empty-UA rows = %d, want 1", got)
	}
	// An invented verb collapses like every metrics rail's method label.
	if got := rollup.Requests[RequestKey{"2026-08-30", "api.muchq.com", 200, "CUSTOM", AgentOther, "(empty)"}]; got != 1 {
		t.Errorf("CUSTOM-method row = %d, want 1", got)
	}
	// The redirect rollup counts per slug and status, across agent classes.
	if got := rollup.Slugs[SlugKey{"2026-08-30", "abc123", 302}]; got != 2 {
		t.Errorf("abc123 hits = %d, want 2", got)
	}
	if got := rollup.Slugs[SlugKey{"2026-08-30", "gone", 404}]; got != 1 {
		t.Errorf("gone-slug 404s = %d, want 1", got)
	}
	// Probe rows exist only for paths that match a scanner family.
	if got := rollup.Probes[ProbeKey{"2026-08-30", "api.muchq.com", ProbeWordpress, 404}]; got != 1 {
		t.Errorf("wordpress probes = %d, want 1", got)
	}
	if got := rollup.Probes[ProbeKey{"2026-08-30", "api.muchq.com", ProbeEnv, 404}]; got != 1 {
		t.Errorf("env probes = %d, want 1", got)
	}
	if len(rollup.Probes) != 2 {
		t.Errorf("probe rows = %v; ordinary routes must not mint any", rollup.Probes)
	}
}

func TestConsumeCapsTheAnonymousAgentTailPerObject(t *testing.T) {
	var lines strings.Builder
	for i := 0; i < maxTailAgentsPerObject+100; i++ {
		fmt.Fprintf(&lines, `{"status":200,"request":{"host":"h","method":"GET","uri":"/","headers":{"User-Agent":["junk-%d/1.0"]}}}`+"\n", i)
	}
	// Marker-named agents arriving after the cap keep their names; only
	// product tokens are capped, and browsers were never named.
	lines.WriteString(`{"status":200,"request":{"host":"h","method":"GET","uri":"/","headers":{"User-Agent":["Mozilla/5.0 (compatible; GPTBot/1.2)"]}}}` + "\n")
	lines.WriteString(`{"status":200,"request":{"host":"h","method":"GET","uri":"/","headers":{"User-Agent":["curl/8.6.0"]}}}` + "\n")
	lines.WriteString(`{"status":200,"request":{"host":"h","method":"GET","uri":"/","headers":{"User-Agent":["Mozilla/5.0 (Macintosh) Chrome/126.0"]}}}` + "\n")
	rollup := NewRollup()

	if _, err := rollup.Consume(strings.NewReader(lines.String()), "2026-08-30"); err != nil {
		t.Fatal(err)
	}

	var total, overflow int64
	names := map[string]bool{}
	for key, count := range rollup.Requests {
		total += count
		if key.Agent == overflowAgent {
			overflow += count
		}
		if key.AgentClass == AgentOther {
			names[key.Agent] = true
		}
	}
	if total != int64(maxTailAgentsPerObject+103) {
		t.Errorf("total requests = %d; the cap must not lose a count", total)
	}
	if overflow != 100 {
		t.Errorf("overflow row = %d, want the 100 past the cap", overflow)
	}
	if len(names) != maxTailAgentsPerObject+1 {
		t.Errorf("distinct other-class names = %d, want the cap plus the overflow row", len(names))
	}
	if rollup.Requests[RequestKey{"2026-08-30", "h", 200, "GET", AgentAIScraper, "gptbot"}] != 1 ||
		rollup.Requests[RequestKey{"2026-08-30", "h", 200, "GET", AgentBot, "curl"}] != 1 ||
		rollup.Requests[RequestKey{"2026-08-30", "h", 200, "GET", AgentBrowser, ""}] != 1 {
		t.Errorf("marker-named and browser rows were caught by the cap: %v", rollup.Requests)
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
