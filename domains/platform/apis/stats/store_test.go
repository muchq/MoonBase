package stats

import (
	"context"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"
)

// Real-database coverage for the store: schema, the processed-marker
// transaction, upsert accumulation, and every read query. Gated the same
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

// The database persists across runs, so every run works under its own
// host and object key; that is what lets the assertions below be exact.
func uniqueFixture(t *testing.T) (date, host, key string) {
	t.Helper()
	nanos := time.Now().UnixNano()
	date = time.Now().UTC().Format("2006-01-02")
	host = fmt.Sprintf("host-%d.example", nanos)
	key = fmt.Sprintf("logs/source=caddy/dt=%s/%s-%d.log.gz", date, t.Name(), nanos)
	return date, host, key
}

func TestApplyRollupIsTransactionalIdempotentAndReadable(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)

	rollup := NewRollup()
	rollup.Requests[RequestKey{date, host, 200, "GET", AgentBrowser, ""}] = 5
	rollup.Requests[RequestKey{date, host, 403, "GET", AgentAIScraper, "gptbot"}] = 2
	rollup.Requests[RequestKey{date, host, 200, "GET", AgentAIScraper, "gptbot"}] = 1
	rollup.Requests[RequestKey{date, host, 404, "GET", AgentOther, "(empty)"}] = 4
	rollup.Slugs[SlugKey{date, host + "-slug", 302}] = 3
	rollup.Probes[ProbeKey{date, host, ProbeEnv, 404}] = 4
	rollup.Probes[ProbeKey{date, host, ProbeEnv, 200}] = 1
	// The query rollup has no host; the store applies any entry, so this run's
	// unique host serves as one and keeps the rows tellable and the counts exact.
	rollup.Queries[QueryKey{date, host, "ui", "ok", "live"}] = 2
	rollup.Terms[TermKey{date, host, KindField, "white.elo"}] = 2

	if pending, err := store.Unprocessed(ctx, []string{key}); err != nil || len(pending) != 1 {
		t.Fatalf("Unprocessed = (%v, %v), want the fresh key pending", pending, err)
	}
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}
	// Applying the same object twice must not double-count: the marker's
	// conflict arm turns the second application into a no-op. The exact
	// counts below are what prove it.
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
	got := map[string]SummaryRow{}
	for _, row := range summary {
		if row.Host == host {
			got[row.AgentClass] = row
		}
	}
	if row := got[AgentBrowser]; row.Requests != 5 || row.Errors != 0 {
		t.Errorf("browser row = %+v, want 5 requests and no errors", row)
	}
	if row := got[AgentAIScraper]; row.Requests != 3 || row.Errors != 2 {
		t.Errorf("scraper row = %+v, want 3 requests with the 403s as errors", row)
	}
	if row := got[AgentOther]; row.Requests != 4 || row.Errors != 4 {
		t.Errorf("other row = %+v, want 4 requests, all errors", row)
	}

	slugs, err := store.TopSlugs(ctx, 2, 1000)
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, row := range slugs {
		if row.Slug == host+"-slug" {
			found = true
			if row.Requests != 3 {
				t.Errorf("slug row = %+v, want 3", row)
			}
		}
	}
	if !found {
		t.Errorf("%s-slug missing from top slugs: %v", host, slugs)
	}

	agents, err := store.Agents(ctx, 2, 2000)
	if err != nil {
		t.Fatal(err)
	}
	named := map[string]AgentRow{}
	for _, row := range agents {
		if row.Host == host {
			named[row.AgentClass+" "+row.Agent] = row
		}
	}
	if row := named[AgentAIScraper+" gptbot"]; row.Requests != 3 || row.Blocked != 2 || row.Date != date {
		t.Errorf("gptbot row = %+v, want 3 requests across statuses, 2 blocked, on %s", row, date)
	}
	if row := named[AgentOther+" (empty)"]; row.Requests != 4 || row.Blocked != 0 {
		t.Errorf("empty-UA row = %+v; a 404 is not a block", row)
	}
	if row := named[AgentBrowser+" "]; row.Requests != 5 {
		t.Errorf("browser row = %+v, want the unnamed bucket carried through", row)
	}

	probes, err := store.Probes(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	var env *ProbeRow
	for i := range probes {
		if probes[i].Host == host && probes[i].Probe == ProbeEnv {
			env = &probes[i]
		}
	}
	if env == nil || env.Requests != 5 || env.Served != 1 {
		t.Errorf("env probe row = %+v; want 5 across statuses with the one 200 served", env)
	}

	if row := queryRowFor(t, store, host); row == nil || row.Requests != 2 || row.Cache != "live" {
		t.Errorf("query row = %+v, want exactly the 2 applied once", row)
	}
	if row := termRowFor(t, store, host); row == nil || row.Requests != 2 || row.Kind != KindField {
		t.Errorf("term row = %+v, want exactly the 2 applied once", row)
	}

	// A second object sharing the keys accumulates: that is the production path, one
	// hourly roll after another into the same day's rows.
	second := NewRollup()
	second.Queries[QueryKey{date, host, "ui", "ok", "live"}] = 3
	second.Terms[TermKey{date, host, KindField, "white.elo"}] = 1
	if err := store.ApplyRollup(ctx, key+".second", second); err != nil {
		t.Fatal(err)
	}
	if row := queryRowFor(t, store, host); row == nil || row.Requests != 5 {
		t.Errorf("query row after a second object = %+v, want 5", row)
	}
	if row := termRowFor(t, store, host); row == nil || row.Requests != 3 {
		t.Errorf("term row after a second object = %+v, want 3", row)
	}
}

func queryRowFor(t *testing.T, store *Store, entry string) *QueryRow {
	t.Helper()
	rows, err := store.Queries(context.Background(), 2)
	if err != nil {
		t.Fatal(err)
	}
	for i := range rows {
		if rows[i].Entry == entry {
			return &rows[i]
		}
	}
	return nil
}

func termRowFor(t *testing.T, store *Store, entry string) *TermRow {
	t.Helper()
	rows, err := store.QueryTerms(context.Background(), 2, 1000)
	if err != nil {
		t.Fatal(err)
	}
	for i := range rows {
		if rows[i].Entry == entry {
			return &rows[i]
		}
	}
	return nil
}

func TestAgentsHonoursTheLimitBusiestFirst(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)

	rollup := NewRollup()
	rollup.Requests[RequestKey{date, host, 200, "GET", AgentBot, "curl"}] = 50
	rollup.Requests[RequestKey{date, host, 200, "GET", AgentBot, "wget"}] = 5
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}

	agents, err := store.Agents(ctx, 2, 1)
	if err != nil {
		t.Fatal(err)
	}
	if len(agents) != 1 {
		t.Fatalf("Agents(limit=1) returned %d rows", len(agents))
	}
	// Other runs' rows share the window, so the survivor is whichever row
	// is busiest overall; it must at least outrank this run's small one.
	if agents[0].Requests < 50 {
		t.Errorf("the one row kept was %+v; want the busiest, not the first", agents[0])
	}
}

func TestAVersionBumpDropsAggregatesAndMarkersForReaggregation(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)
	rollup := NewRollup()
	rollup.Requests[RequestKey{date, host, 200, "GET", AgentBot, "curl"}] = 1
	rollup.Slugs[SlugKey{date, host + "-slug", 302}] = 1
	rollup.Probes[ProbeKey{date, host, ProbeGit, 404}] = 1
	rollup.Queries[QueryKey{date, host, "ui", "ok", "live"}] = 1
	rollup.Terms[TermKey{date, host, KindField, "eco"}] = 1
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}

	// Pretend those aggregates came from an older rollup shape.
	if _, err := store.pool.Exec(ctx,
		`UPDATE stats_meta SET value = 'stale' WHERE key = 'rollup_version'`); err != nil {
		t.Fatal(err)
	}
	reopened, err := NewStore(ctx, os.Getenv("STATS_TEST_DB_URL"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(reopened.Close)

	// The marker is gone, so the next pass recomputes the object; every
	// aggregate it produced is gone with it, so nothing is double-counted.
	if pending, err := reopened.Unprocessed(ctx, []string{key}); err != nil || len(pending) != 1 {
		t.Errorf("Unprocessed after a version bump = (%v, %v), want the key pending again", pending, err)
	}
	summary, err := reopened.Summary(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	for _, row := range summary {
		if row.Host == host {
			t.Errorf("request aggregates survived the version bump: %+v", row)
		}
	}
	agents, err := reopened.Agents(ctx, 2, 2000)
	if err != nil {
		t.Fatal(err)
	}
	for _, row := range agents {
		if row.Host == host {
			t.Errorf("agent aggregates survived the version bump: %+v", row)
		}
	}
	slugs, err := reopened.TopSlugs(ctx, 2, 1000)
	if err != nil {
		t.Fatal(err)
	}
	for _, row := range slugs {
		if row.Slug == host+"-slug" {
			t.Errorf("slug aggregates survived the version bump: %+v", row)
		}
	}
	probes, err := reopened.Probes(ctx, 2)
	if err != nil {
		t.Fatal(err)
	}
	for _, row := range probes {
		if row.Host == host {
			t.Errorf("probe aggregates survived the version bump: %+v", row)
		}
	}
	if row := queryRowFor(t, reopened, host); row != nil {
		t.Errorf("query aggregates survived the version bump: %+v", row)
	}
	if row := termRowFor(t, reopened, host); row != nil {
		t.Errorf("term aggregates survived the version bump: %+v", row)
	}
	var recorded string
	if err := reopened.pool.QueryRow(ctx,
		`SELECT value FROM stats_meta WHERE key = 'rollup_version'`).Scan(&recorded); err != nil || recorded != currentRollupVersion() {
		t.Errorf("recorded version = (%q, %v), want %q", recorded, err, currentRollupVersion())
	}

	// Reopening at the same version is a no-op: the store must not wipe
	// itself on every boot.
	if err := reopened.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}
	again, err := NewStore(ctx, os.Getenv("STATS_TEST_DB_URL"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(again.Close)
	if pending, err := again.Unprocessed(ctx, []string{key}); err != nil || len(pending) != 0 {
		t.Errorf("Unprocessed after a same-version reopen = (%v, %v), want none", pending, err)
	}
}

// A column added to a table without anyone remembering to bump the
// constant must still re-aggregate: the recorded version carries the DDL.
func TestTheRecordedVersionFollowsTheSchemaText(t *testing.T) {
	changed := append([]string{}, schema...)
	changed[1] = strings.Replace(changed[1], "agent text NOT NULL,", "agent text NOT NULL,\n\t\textra int,", 1)
	if changed[1] == schema[1] {
		t.Fatal("the fixture did not change the DDL; the test proves nothing")
	}
	if rollupVersionFor(RollupVersion, changed) == rollupVersionFor(RollupVersion, schema) {
		t.Error("a DDL change left the rollup version unchanged")
	}
	if rollupVersionFor("3", schema) == rollupVersionFor(RollupVersion, schema) {
		t.Error("a meaning bump left the rollup version unchanged")
	}
}

// A version change recreates the tables rather than emptying them, so a
// table whose columns changed shape comes back in the new shape.
func TestAVersionChangeRecreatesTablesInTheirNewShape(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)

	// An older deployment's request_stats, without the agent column.
	for _, ddl := range []string{
		`DROP TABLE request_stats`,
		`CREATE TABLE request_stats (dt date NOT NULL, host text NOT NULL, requests bigint NOT NULL)`,
		`UPDATE stats_meta SET value = 'older' WHERE key = 'rollup_version'`,
	} {
		if _, err := store.pool.Exec(ctx, ddl); err != nil {
			t.Fatal(err)
		}
	}

	reopened, err := NewStore(ctx, os.Getenv("STATS_TEST_DB_URL"))
	if err != nil {
		t.Fatalf("reopening over an old-shaped table: %v", err)
	}
	t.Cleanup(reopened.Close)
	rollup := NewRollup()
	rollup.Requests[RequestKey{date, host, 200, "GET", AgentBot, "curl"}] = 1
	if err := reopened.ApplyRollup(ctx, key, rollup); err != nil {
		t.Errorf("the new-shape insert failed after the version change: %v", err)
	}
}
