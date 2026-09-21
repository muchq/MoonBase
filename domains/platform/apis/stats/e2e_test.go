package stats

import (
	"bytes"
	"compress/gzip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"
)

// The whole service, end to end, through the objects production uses: the
// real vendor file through Locate, shipped objects in an in-memory bucket
// through Aggregator.RunOnce, the real Store on a real Postgres, and the
// real router over HTTP. Each layer has its own tests; this is the one that
// says they fit, and the example to copy for the next endpoint.
//
// Gated on STATS_TEST_DB_URL like the store tests: without it this skips,
// and CI supplies the URL from its postgres service.

// memoryBucket is the shipper's layout in memory: keys as the shipper
// writes them, gzipped bodies as it uploads them.
type memoryBucket map[string][]byte

func (b memoryBucket) List(prefix string) ([]string, error) {
	var keys []string
	for key := range b {
		if strings.HasPrefix(key, prefix) {
			keys = append(keys, key)
		}
	}
	return keys, nil
}

func (b memoryBucket) Get(key string) (io.ReadCloser, error) {
	body, ok := b[key]
	if !ok {
		return nil, fs.ErrNotExist
	}
	return io.NopCloser(bytes.NewReader(body)), nil
}

func gzipObject(t *testing.T, lines ...string) []byte {
	t.Helper()
	var buf bytes.Buffer
	w := gzip.NewWriter(&buf)
	if _, err := w.Write([]byte(strings.Join(lines, "\n") + "\n")); err != nil {
		t.Fatal(err)
	}
	if err := w.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func caddyLineFor(host, method, uri, ip, agent string, status int) string {
	return caddyLineFrom(host, method, uri, ip, agent, "", status)
}

// The same line with an Origin, which is what tells the caller apart.
func caddyLineFrom(host, method, uri, ip, agent, origin string, status int) string {
	headers := fmt.Sprintf(`"User-Agent":[%q]`, agent)
	if origin != "" {
		headers += fmt.Sprintf(`,"Origin":[%q]`, origin)
	}
	return fmt.Sprintf(`{"status":%d,"request":{"host":%q,"method":%q,"uri":%q,"client_ip":%q,"headers":{%s}}}`,
		status, host, method, uri, ip, headers)
}

func getJSON(t *testing.T, server *httptest.Server, path string) []map[string]any {
	t.Helper()
	response, err := http.Get(server.URL + path)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("GET %s = %d", path, response.StatusCode)
	}
	var body struct {
		Rows []map[string]any `json:"rows"`
	}
	if err := json.NewDecoder(response.Body).Decode(&body); err != nil {
		t.Fatalf("GET %s: %v", path, err)
	}
	return body.Rows
}

// rowsWhere keeps the rows whose fields all match; the database is shared
// across runs, so no assertion may read a bare total.
func rowsWhere(rows []map[string]any, match map[string]any) []map[string]any {
	var out []map[string]any
	for _, row := range rows {
		ok := true
		for key, want := range match {
			if row[key] != want {
				ok = false
			}
		}
		if ok {
			out = append(out, row)
		}
	}
	return out
}

// sum adds one numeric field over the rows whose fields all match.
func sum(rows []map[string]any, match map[string]any, field string) float64 {
	var total float64
	for _, row := range rowsWhere(rows, match) {
		total += row[field].(float64)
	}
	return total
}

// The five endpoints keyed by host, read together so an assertion can be a
// delta between two reads rather than an absolute count.
type endpointRows struct{ summary, services, agents, probes, countries []map[string]any }

func readEndpoints(t *testing.T, server *httptest.Server) endpointRows {
	t.Helper()
	return endpointRows{
		summary:   getJSON(t, server, "/stats/v1/summary?days=2"),
		services:  getJSON(t, server, "/stats/v1/services?days=2&limit=5000"),
		agents:    getJSON(t, server, "/stats/v1/agents?days=2&limit=2000"),
		probes:    getJSON(t, server, "/stats/v1/probes?days=2"),
		countries: getJSON(t, server, "/stats/v1/countries?days=2&limit=5000"),
	}
}

func TestEndToEndFromShippedObjectsToEveryEndpoint(t *testing.T) {
	url := os.Getenv("STATS_TEST_DB_URL")
	if url == "" {
		t.Skip("STATS_TEST_DB_URL not set; skipping the end-to-end test")
	}
	ctx := context.Background()
	date := time.Now().UTC().Format("2006-01-02")
	// The host column holds the site now, so a run can no longer isolate
	// itself by inventing a host: the fixtures address a vhost caddy really
	// serves, and every count below is a delta across this run's one
	// aggregation. The run's own token still keys the objects, which is what
	// keeps the processed markers and the short-link row its own.
	const site = "api.muchq.com"
	run := fmt.Sprintf("e2e-%d", time.Now().UnixNano())

	geo, _, err := Locate(vendorFile(t))
	if err != nil {
		t.Fatal(err)
	}
	bucket := memoryBucket{
		// Two caddy rolls for the same day, so the day's rows accumulate
		// across objects the way hourly rolls do in production.
		fmt.Sprintf("logs/source=caddy/dt=%s/%s-a.log.gz", date, run): gzipObject(t,
			// The web app on a route the Caddyfile claims, which is the one
			// line here that carries a route and a caller worth naming.
			caddyLineFrom(site, "GET", "/games/v2/session", "8.8.8.8",
				"Mozilla/5.0 (Macintosh) Chrome/126.0", "https://muchq.com", 200),
			caddyLineFor(site, "GET", "/", "8.8.8.8", "Mozilla/5.0 (Macintosh) Chrome/126.0", 200),
			caddyLineFor(site, "GET", "/x", "57.141.3.4", "meta-externalagent/1.1", 403),
			caddyLineFor(site, "GET", "/.env", "10.1.2.3", "TLM-Audit-Scanner/1.0", 404),
		),
		fmt.Sprintf("logs/source=caddy/dt=%s/%s-b.log.gz", date, run): gzipObject(t,
			caddyLineFor(site, "GET", "/.git/config", "57.141.3.4", "meta-externalagent/1.1", 403),
			caddyLineFor("i.iili.uk", "GET", "/r/"+run, "8.8.8.8", "curl/8.6.0", 302),
		),
		fmt.Sprintf("logs/source=one_d4/dt=%s/%s-events.log.gz", date, run): gzipObject(t,
			eventLine("entry", "query", "source", "ui", "fields", "white.elo", "motifs", "fork",
				"order_by", "", "player", "false", "limit", "10", "offset", "0", "cache", "live",
				"rows", "3", "outcome", "ok", "duration_us", "1500"),
			eventLine("entry", "aggregate", "source", "mcp", "fields", "eco", "motifs", "",
				"order_by", "", "player", "true", "group_by", "eco", "order", "count",
				"min_games", "0", "limit", "20", "rows", "2", "outcome", "ok", "duration_us", "9000"),
		),
	}

	store, err := NewStore(ctx, url)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(store.Close)
	logger := slog.New(slog.NewTextHandler(io.Discard, nil))
	aggregator := &Aggregator{Objects: bucket, Store: store, Logger: logger, Geo: geo}

	server := httptest.NewServer(NewRouter(NewHandlers(store, logger)))
	t.Cleanup(server.Close)
	before := readEndpoints(t, server)

	processed, err := aggregator.RunOnce(ctx)
	if err != nil || processed != 3 {
		t.Fatalf("RunOnce = (%d, %v), want all three objects", processed, err)
	}
	// A second pass finds nothing new: the markers hold, and nothing is
	// counted twice.
	if processed, err := aggregator.RunOnce(ctx); err != nil || processed != 0 {
		t.Fatalf("second RunOnce = (%d, %v), want nothing to do", processed, err)
	}

	after := readEndpoints(t, server)
	grew := func(what string, before, after []map[string]any, match map[string]any, field string, want float64) {
		t.Helper()
		if got := sum(after, match, field) - sum(before, match, field); got != want {
			t.Errorf("%s: %s grew by %v, want %v", what, field, got, want)
		}
	}

	// Summary: the site's five requests across three classes, the two 403s
	// and the 404 as errors.
	for _, want := range []struct {
		class            string
		requests, errors float64
	}{
		{AgentBrowser, 2, 0},
		{AgentAIScraper, 2, 2},
		{AgentBot, 1, 1},
	} {
		match := map[string]any{"host": site, "agent_class": want.class}
		grew("summary "+want.class, before.summary, after.summary, match, "requests", want.requests)
		grew("summary "+want.class, before.summary, after.summary, match, "errors", want.errors)
	}

	// Agents: the scraper by name, both of its requests refused.
	meta := map[string]any{"host": site, "agent": "meta-externalagent"}
	grew("meta-externalagent", before.agents, after.agents, meta, "requests", 2)
	grew("meta-externalagent", before.agents, after.agents, meta, "blocked", 2)

	// Probes: two families on this site, neither served.
	for _, probe := range []string{ProbeEnv, ProbeGit} {
		match := map[string]any{"host": site, "probe": probe}
		grew("probe "+probe, before.probes, after.probes, match, "requests", 1)
		grew("probe "+probe, before.probes, after.probes, match, "served", 0)
	}

	// Countries, through the real vendor file: Google's resolver and
	// Meta's range in the US, the private scanner unplaced.
	for _, want := range []struct {
		class, country            string
		requests, blocked, probes float64
	}{
		{AgentAIScraper, "US", 2, 2, 1},
		{AgentBrowser, "US", 2, 0, 0},
		{AgentBot, UnknownCountry, 1, 0, 1},
	} {
		match := map[string]any{"host": site, "agent_class": want.class, "country": want.country}
		what := want.class + " from " + want.country
		grew(what, before.countries, after.countries, match, "requests", want.requests)
		grew(what, before.countries, after.countries, match, "blocked", want.blocked)
		grew(what, before.countries, after.countries, match, "probes", want.probes)
	}

	// Services: the web app's one routed request, named for the backend
	// that answered it. This is the whole path — a log line's Host and URI
	// through the classifiers, into the route and source columns, back out
	// as the service behind them.
	webApp := map[string]any{
		"host": site, "service": "games_hub", "source": SourceUI, "agent_class": AgentBrowser,
	}
	grew("the web app on games_hub", before.services, after.services, webApp, "requests", 1)
	grew("the web app on games_hub", before.services, after.services, webApp, "errors", 0)

	// And what no backend saw: the browser's unrouted GET, the scraper's
	// two 403s and the scanner's 404. Three of the four are errors, and
	// the fourth is why requests is asserted beside them.
	unserved := map[string]any{"host": site, "service": OtherService, "source": SourceAPI}
	grew("what nothing served", before.services, after.services, unserved, "requests", 4)
	grew("what nothing served", before.services, after.services, unserved, "errors", 3)

	// Short links: the redirect on i.iili.uk, keyed by this run's own slug.
	slugs := rowsWhere(getJSON(t, server, "/stats/v1/iili/top?days=2&limit=200"), map[string]any{"slug": run})
	if len(slugs) != 1 || slugs[0]["requests"] != float64(1) {
		t.Errorf("slug rows = %v", slugs)
	}

	// one_d4 query events: the two entries share the day's rows with every
	// other run, so the assertions are that this run's rows are present and
	// that the term vocabulary came through; counts are lower bounds here.
	queries := getJSON(t, server, "/stats/v1/one_d4/queries?days=2")
	if len(rowsWhere(queries, map[string]any{"entry": "query", "source": "ui", "outcome": "ok", "cache": "live"})) != 1 ||
		len(rowsWhere(queries, map[string]any{"entry": "aggregate", "source": "mcp", "outcome": "ok", "cache": "none"})) != 1 {
		t.Errorf("query rows = %v", queries)
	}
	terms := getJSON(t, server, "/stats/v1/one_d4/terms?days=2&limit=1000")
	if len(rowsWhere(terms, map[string]any{"entry": "aggregate", "kind": KindGroupBy, "term": "eco"})) != 1 ||
		len(rowsWhere(terms, map[string]any{"entry": "query", "kind": KindMotif, "term": "fork"})) != 1 {
		t.Errorf("term rows = %v", terms)
	}

	// And the health probe, which the container healthcheck reads.
	response, err := http.Get(server.URL + "/health")
	if err != nil || response.StatusCode != http.StatusOK {
		t.Errorf("GET /health = %v, %v", response, err)
	}
}
