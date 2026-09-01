package stats

import (
	"context"
	"fmt"
	"os"
	"testing"
	"time"
)

// Real-database coverage for the store: schema, the processed-marker
// transaction, upsert accumulation, and both read queries. Gated the same
// way the repo's other Postgres suites are: without STATS_TEST_DB_URL this
// skips, and CI supplies the URL from its postgres service.
func testStore(t *testing.T) *Store {
	t.Helper()
	url := os.Getenv("STATS_TEST_DB_URL")
	if url == "" {
		t.Skip("STATS_TEST_DB_URL not set; skipping store integration test")
	}
	store, err := NewStore(context.Background(), url)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(store.Close)
	return store
}

func TestApplyRollupIsTransactionalIdempotentAndReadable(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	// Unique key per run: the database persists across test runs.
	key := fmt.Sprintf("logs/source=caddy/dt=%s/test-%d.log.gz",
		time.Now().UTC().Format("2006-01-02"), time.Now().UnixNano())
	date := time.Now().UTC().Format("2006-01-02")

	rollup := NewRollup()
	rollup.Requests[RequestKey{date, "test-host.example", 200, "GET", AgentBrowser}] = 5
	rollup.Requests[RequestKey{date, "test-host.example", 403, "GET", AgentAIScraper}] = 2
	rollup.Slugs[SlugKey{date, "test-slug", 302}] = 3

	if pending, err := store.Unprocessed(ctx, []string{key}); err != nil || len(pending) != 1 {
		t.Fatalf("Unprocessed = (%v, %v), want the fresh key pending", pending, err)
	}
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}
	// Applying the same object twice must not double-count: the marker's
	// conflict arm turns the second application into a no-op.
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}
	if pending, err := store.Unprocessed(ctx, []string{key}); err != nil || len(pending) != 0 {
		t.Fatalf("Unprocessed after apply = (%v, %v), want none", pending, err)
	}

	summary, err := store.Summary(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	var browser, scraper *SummaryRow
	for i := range summary {
		row := &summary[i]
		if row.Host == "test-host.example" && row.AgentClass == AgentBrowser {
			browser = row
		}
		if row.Host == "test-host.example" && row.AgentClass == AgentAIScraper {
			scraper = row
		}
	}
	if browser == nil || browser.Requests < 5 || browser.Errors != 0 {
		t.Errorf("browser row = %+v", browser)
	}
	if scraper == nil || scraper.Requests < 2 || scraper.Errors < 2 {
		t.Errorf("scraper row = %+v; 403s must count as errors", scraper)
	}

	slugs, err := store.TopSlugs(ctx, 2, 100)
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, row := range slugs {
		if row.Slug == "test-slug" && row.Requests >= 3 {
			found = true
		}
	}
	if !found {
		t.Errorf("test-slug missing from top slugs: %v", slugs)
	}
}
