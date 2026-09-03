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

type fakeLocator map[string]string

func (f fakeLocator) Country(ip string) string { return f[ip] }

func TestConsumeRollsUpWhereEachClassCameFromWithBlocksAndProbes(t *testing.T) {
	lines := strings.Join([]string{
		`{"status":200,"request":{"host":"h","method":"GET","uri":"/","client_ip":"1.0.0.7","headers":{"User-Agent":["Mozilla/5.0 (Macintosh) Chrome/126.0"]}}}`,
		`{"status":403,"request":{"host":"h","method":"GET","uri":"/x","client_ip":"57.141.3.4","headers":{"User-Agent":["meta-externalagent/1.1"]}}}`,
		`{"status":404,"request":{"host":"h","method":"GET","uri":"/.env","client_ip":"195.178.110.199","headers":{"User-Agent":["TLM-Audit-Scanner/1.0"]}}}`,
		`{"status":404,"request":{"host":"h","method":"GET","uri":"/wp-login.php","client_ip":"195.178.110.199","headers":{"User-Agent":["TLM-Audit-Scanner/1.0"]}}}`,
		// Refused and a probe at once: the production shape once refuse_bots answers a scanner.
		`{"status":403,"request":{"host":"h","method":"GET","uri":"/.git/config","client_ip":"195.178.110.199","headers":{"User-Agent":["TLM-Audit-Scanner/1.0"]}}}`,
		// Older lines carry remote_ip only; an address the locator does not know is "--".
		`{"status":200,"request":{"host":"h","method":"GET","uri":"/","remote_ip":"10.1.2.3","headers":{"User-Agent":["curl/8.6.0"]}}}`,
	}, "\n")
	rollup := NewRollup()
	rollup.Geo = fakeLocator{"1.0.0.7": "AU", "57.141.3.4": "US", "195.178.110.199": "GB"}

	if _, err := rollup.Consume(strings.NewReader(lines), "2026-08-30"); err != nil {
		t.Fatal(err)
	}

	want := map[GeoKey]GeoStat{
		{"2026-08-30", "h", AgentBrowser, "AU"}:   {1, 0, 0},
		{"2026-08-30", "h", AgentAIScraper, "US"}: {1, 1, 0},
		{"2026-08-30", "h", AgentBot, "GB"}:       {3, 1, 3},
		{"2026-08-30", "h", AgentBot, "--"}:       {1, 0, 0},
	}
	if len(rollup.Countries) != len(want) {
		t.Errorf("geo rows = %v, want %v", rollup.Countries, want)
	}
	for key, stat := range want {
		if rollup.Countries[key] != stat {
			t.Errorf("geo %+v = %+v, want %+v", key, rollup.Countries[key], stat)
		}
	}
}

// Without a database every row is "--"; the class split still holds, so the
// table is honest rather than empty.
func TestConsumeWithoutAGeoDatabaseFilesEverythingUnderUnknown(t *testing.T) {
	rollup := NewRollup()
	if _, err := rollup.Consume(strings.NewReader(sampleLines), "2026-08-30"); err != nil {
		t.Fatal(err)
	}
	var total int64
	for key, stat := range rollup.Countries {
		if key.Country != UnknownCountry {
			t.Errorf("row %+v placed without a database", key)
		}
		total += stat.Requests
	}
	var requests int64
	for _, count := range rollup.Requests {
		requests += count
	}
	if total != requests {
		t.Errorf("geo rows count %d requests, the request rollup %d; every request is somewhere", total, requests)
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

// Caddy rolls by size, so one object spans whatever days it took to
// fill: each line lands on its own day, and only a line with no
// timestamp takes the object's.
func TestConsumeDatesEachLineByItsOwnTimestamp(t *testing.T) {
	lines := `{"ts":1788220800.5,"status":200,"request":{"host":"api.muchq.com","method":"GET","uri":"/a","headers":{}}}
{"ts":1788393599.9,"status":200,"request":{"host":"api.muchq.com","method":"GET","uri":"/b","headers":{}}}
{"ts":1788393600.0,"status":200,"request":{"host":"api.muchq.com","method":"GET","uri":"/c","headers":{}}}
{"status":200,"request":{"host":"api.muchq.com","method":"GET","uri":"/d","headers":{}}}
`
	rollup := NewRollup()
	if _, err := rollup.Consume(strings.NewReader(lines), "2026-09-03"); err != nil {
		t.Fatal(err)
	}
	byDate := map[string]int64{}
	for key, count := range rollup.Requests {
		byDate[key.Date] += count
	}
	want := map[string]int64{
		"2026-09-01": 1, // 1788220800 is 2026-09-01T00:00:00Z
		"2026-09-02": 1, // the last second of the 2nd
		"2026-09-03": 2, // midnight on the 3rd, and the line with no ts
	}
	if fmt.Sprint(byDate) != fmt.Sprint(want) {
		t.Errorf("requests by date = %v, want %v", byDate, want)
	}
	for key := range rollup.Countries {
		if key.Date == "" {
			t.Errorf("geo row with no date: %v", key)
		}
	}
}
