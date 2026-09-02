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
	return fmt.Sprintf(`{"status":%d,"request":{"host":%q,"method":%q,"uri":%q,"client_ip":%q,"headers":{"User-Agent":[%q]}}}`,
		status, host, method, uri, ip, agent)
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
// across runs, so every assertion is scoped to this run's own host.
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

func TestEndToEndFromShippedObjectsToEveryEndpoint(t *testing.T) {
	url := os.Getenv("STATS_TEST_DB_URL")
	if url == "" {
		t.Skip("STATS_TEST_DB_URL not set; skipping the end-to-end test")
	}
	ctx := context.Background()
	date := time.Now().UTC().Format("2006-01-02")
	host := fmt.Sprintf("e2e-%d.example", time.Now().UnixNano())

	geo, _, err := Locate(vendorFile(t))
	if err != nil {
		t.Fatal(err)
	}
	bucket := memoryBucket{
		// Two caddy rolls for the same day, so the day's rows accumulate
		// across objects the way hourly rolls do in production.
		fmt.Sprintf("logs/source=caddy/dt=%s/%s-a.log.gz", date, host): gzipObject(t,
			caddyLineFor(host, "GET", "/", "8.8.8.8", "Mozilla/5.0 (Macintosh) Chrome/126.0", 200),
			caddyLineFor(host, "GET", "/", "8.8.8.8", "Mozilla/5.0 (Macintosh) Chrome/126.0", 200),
			caddyLineFor(host, "GET", "/x", "57.141.3.4", "meta-externalagent/1.1", 403),
			caddyLineFor(host, "GET", "/.env", "10.1.2.3", "TLM-Audit-Scanner/1.0", 404),
		),
		fmt.Sprintf("logs/source=caddy/dt=%s/%s-b.log.gz", date, host): gzipObject(t,
			caddyLineFor(host, "GET", "/.git/config", "57.141.3.4", "meta-externalagent/1.1", 403),
			caddyLineFor("i.iili.uk", "GET", "/r/"+host, "8.8.8.8", "curl/8.6.0", 302),
		),
		fmt.Sprintf("logs/source=one_d4/dt=%s/%s-events.log.gz", date, host): gzipObject(t,
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

	processed, err := aggregator.RunOnce(ctx)
	if err != nil || processed != 3 {
		t.Fatalf("RunOnce = (%d, %v), want all three objects", processed, err)
	}
	// A second pass finds nothing new: the markers hold, and nothing is
	// counted twice.
	if processed, err := aggregator.RunOnce(ctx); err != nil || processed != 0 {
		t.Fatalf("second RunOnce = (%d, %v), want nothing to do", processed, err)
	}

	server := httptest.NewServer(NewRouter(NewHandlers(store, logger)))
	t.Cleanup(server.Close)

	// Summary: the host's five requests across three classes, the two 403s
	// and the 404 as errors.
	summary := rowsWhere(getJSON(t, server, "/stats/v1/summary?days=2"), map[string]any{"host": host})
	byClass := map[string][2]float64{}
	for _, row := range summary {
		byClass[row["agent_class"].(string)] = [2]float64{row["requests"].(float64), row["errors"].(float64)}
	}
	if byClass["browser"] != [2]float64{2, 0} || byClass["ai_scraper"] != [2]float64{2, 2} || byClass["bot"] != [2]float64{1, 1} {
		t.Errorf("summary by class = %v", byClass)
	}

	// Agents: the scraper by name, both of its requests refused.
	meta := rowsWhere(getJSON(t, server, "/stats/v1/agents?days=2&limit=2000"),
		map[string]any{"host": host, "agent": "meta-externalagent"})
	if len(meta) != 1 || meta[0]["requests"] != float64(2) || meta[0]["blocked"] != float64(2) {
		t.Errorf("meta rows = %v", meta)
	}

	// Probes: two families on this host, neither served.
	probes := rowsWhere(getJSON(t, server, "/stats/v1/probes?days=2"), map[string]any{"host": host})
	families := map[string][2]float64{}
	for _, row := range probes {
		families[row["probe"].(string)] = [2]float64{row["requests"].(float64), row["served"].(float64)}
	}
	if families[ProbeEnv] != [2]float64{1, 0} || families[ProbeGit] != [2]float64{1, 0} {
		t.Errorf("probe families = %v", families)
	}

	// Countries, through the real vendor file: Google's resolver and
	// Meta's range in the US, the private scanner unplaced.
	countries := rowsWhere(getJSON(t, server, "/stats/v1/countries?days=2&limit=5000"), map[string]any{"host": host})
	placed := map[string]map[string]any{}
	for _, row := range countries {
		placed[row["agent_class"].(string)+"/"+row["country"].(string)] = row
	}
	if row := placed["ai_scraper/US"]; row == nil || row["requests"] != float64(2) || row["blocked"] != float64(2) || row["probes"] != float64(1) {
		t.Errorf("scraper from US = %v", row)
	}
	if row := placed["browser/US"]; row == nil || row["requests"] != float64(2) {
		t.Errorf("browsers from US = %v", row)
	}
	if row := placed["bot/"+UnknownCountry]; row == nil || row["requests"] != float64(1) || row["probes"] != float64(1) {
		t.Errorf("private-address bot = %v", row)
	}

	// Short links: the redirect on i.iili.uk, keyed by this run's slug.
	slugs := rowsWhere(getJSON(t, server, "/stats/v1/iili/top?days=2&limit=200"), map[string]any{"slug": host})
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
