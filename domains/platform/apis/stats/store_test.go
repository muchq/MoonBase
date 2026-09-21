package stats

import (
	"context"
	"fmt"
	"os"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
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
	row := func(status int, class, agent, route, source string) RequestKey {
		return RequestKey{
			Date: date, Host: host, Route: route, Source: source,
			Status: status, Method: "GET", AgentClass: class, Agent: agent,
		}
	}
	rollup.Requests[row(200, AgentBrowser, "", "/games/v2/session", SourceUI)] = 5
	// And two rows differing only in route: the column names the backend
	// that served the request, so folding these loses which one did.
	rollup.Requests[row(200, AgentBot, "curl", "/games/v2/play", SourceAPI)] = 7
	rollup.Requests[row(200, AgentBot, "curl", "/games/v2/session", SourceAPI)] = 3
	// Same row but for the caller: the widened primary key has to keep the
	// two apart rather than summing them.
	rollup.Requests[row(200, AgentBrowser, "", "/games/v2/session", SourceAPI)] = 3
	rollup.Requests[row(403, AgentAIScraper, "gptbot", OtherRoute, SourceAPI)] = 2
	rollup.Requests[row(200, AgentAIScraper, "gptbot", OtherRoute, SourceAPI)] = 1
	rollup.Requests[row(404, AgentOther, "(empty)", OtherRoute, SourceAPI)] = 4
	rollup.Slugs[SlugKey{date, host + "-slug", 302}] = 3
	rollup.Probes[ProbeKey{Date: date, Host: host, Route: OtherRoute, Probe: ProbeEnv, Status: 404}] = 4
	rollup.Probes[ProbeKey{Date: date, Host: host, Route: OtherRoute, Probe: ProbeEnv, Status: 200}] = 1
	// Same probe, same status, a different backend: the route is in this
	// key too, so it has to keep these apart the way source does above.
	rollup.Probes[ProbeKey{Date: date, Host: host, Route: "/deja/v1/*", Probe: ProbeEnv, Status: 404}] = 2
	// The query rollup has no host; the store applies any entry, so this run's
	// unique host serves as one and keeps the rows tellable and the counts exact.
	rollup.Queries[QueryKey{date, host, "ui", "ok", "live"}] = 2
	rollup.Terms[TermKey{date, host, KindField, "white.elo"}] = 2
	rollup.Countries[GeoKey{date, host, AgentBot, "GB"}] = GeoStat{5, 1, 4}
	rollup.Countries[GeoKey{date, host, AgentBot, "--"}] = GeoStat{2, 0, 0}

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

	// What keeps these rows apart is the widened primary key. No read
	// exposes route or source yet — the endpoints all group across them —
	// so this is the one place that says the upsert filed them as separate
	// rows rather than folding them into one.
	byColumn := func(column, table, class string) map[string]int64 {
		t.Helper()
		out := map[string]int64{}
		rows, err := store.pool.Query(ctx,
			"SELECT "+column+", requests FROM "+table+" WHERE host = $1 AND "+class, host)
		if err != nil {
			t.Fatal(err)
		}
		for rows.Next() {
			var value string
			var requests int64
			if err := rows.Scan(&value, &requests); err != nil {
				t.Fatal(err)
			}
			out[value] = requests
		}
		if err := rows.Err(); err != nil {
			t.Fatal(err)
		}
		return out
	}
	if got := byColumn("source", "request_stats", "agent_class = '"+AgentBrowser+"'"); len(got) != 2 ||
		got[SourceUI] != 5 || got[SourceAPI] != 3 {
		t.Errorf("browser rows by source = %v, want ui 5 and api 3 kept apart", got)
	}
	if got := byColumn("route", "request_stats", "agent_class = '"+AgentBot+"'"); len(got) != 2 ||
		got["/games/v2/play"] != 7 || got["/games/v2/session"] != 3 {
		t.Errorf("bot rows by route = %v, want the two backends kept apart", got)
	}
	if got := byColumn("route", "probe_stats", "status = 404"); len(got) != 2 ||
		got[OtherRoute] != 4 || got["/deja/v1/*"] != 2 {
		t.Errorf("probe rows by route = %v, want the two backends kept apart", got)
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
	// Every read groups across source, so the browser's two rows arrive summed.
	if row := got[AgentBrowser]; row.Requests != 8 || row.Errors != 0 {
		t.Errorf("browser row = %+v, want 5 + 3 requests and no errors", row)
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
	if row := named[AgentBrowser+" "]; row.Requests != 8 {
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
	// The endpoint groups across route, so the two backends' rows arrive summed.
	if env == nil || env.Requests != 7 || env.Served != 1 {
		t.Errorf("env probe row = %+v; want 7 across statuses and routes with the one 200 served", env)
	}

	if row := queryRowFor(t, store, host); row == nil || row.Requests != 2 || row.Cache != "live" {
		t.Errorf("query row = %+v, want exactly the 2 applied once", row)
	}
	if row := countryRowFor(t, store, host, "GB"); row == nil || *row != (CountryRow{host, AgentBot, "GB", 5, 1, 4}) {
		t.Errorf("country row = %+v, want exactly the rollup applied once", row)
	}
	if row := countryRowFor(t, store, host, UnknownCountry); row == nil || *row != (CountryRow{host, AgentBot, "--", 2, 0, 0}) {
		t.Errorf("unplaced row = %+v, want it stored and read back like any country", row)
	}
	if row := termRowFor(t, store, host); row == nil || row.Requests != 2 || row.Kind != KindField {
		t.Errorf("term row = %+v, want exactly the 2 applied once", row)
	}

	// A second object sharing the keys accumulates: that is the production path, one
	// hourly roll after another into the same day's rows.
	second := NewRollup()
	second.Queries[QueryKey{date, host, "ui", "ok", "live"}] = 3
	second.Terms[TermKey{date, host, KindField, "white.elo"}] = 1
	second.Countries[GeoKey{date, host, AgentBot, "GB"}] = GeoStat{1, 1, 0}
	if err := store.ApplyRollup(ctx, key+".second", second); err != nil {
		t.Fatal(err)
	}
	if row := countryRowFor(t, store, host, "GB"); row == nil || *row != (CountryRow{host, AgentBot, "GB", 6, 2, 4}) {
		t.Errorf("country row after a second object = %+v, want the three columns summed", row)
	}
	if row := queryRowFor(t, store, host); row == nil || row.Requests != 5 {
		t.Errorf("query row after a second object = %+v, want 5", row)
	}
	if row := termRowFor(t, store, host); row == nil || row.Requests != 3 {
		t.Errorf("term row after a second object = %+v, want 3", row)
	}
}

func countryRowFor(t *testing.T, store *Store, host, country string) *CountryRow {
	t.Helper()
	rows, err := store.Countries(context.Background(), 2, 5000)
	if err != nil {
		t.Fatal(err)
	}
	for i := range rows {
		if rows[i].Host == host && rows[i].Country == country {
			return &rows[i]
		}
	}
	return nil
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
	rollup.Requests[RequestKey{Date: date, Host: host, Route: OtherRoute, Source: SourceAPI, Status: 200, Method: "GET", AgentClass: AgentBot, Agent: "curl"}] = 50
	rollup.Requests[RequestKey{Date: date, Host: host, Route: OtherRoute, Source: SourceAPI, Status: 200, Method: "GET", AgentClass: AgentBot, Agent: "wget"}] = 5
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

// rollupTables is what a version bump drops, and its comment says every
// aggregate table in schema belongs to it. Nothing enforced that: an
// eighth table would be created by the schema, missed by the drop, and
// left holding rows keyed the old way — double-counted against the rows
// recomputed beside them. This reads the DDL instead of naming the tables
// by hand, so the list cannot fall behind the schema it describes.
// Two boots of a database that has never recorded a version serialize,
// and the second one reads what the first wrote.
//
// FOR UPDATE locks nothing when there is no row to lock, so unseeded both
// boots read "no version", both decide to drop, and the later one drops
// the tables the earlier had already recreated and filled. Seeding the row
// first puts the second boot behind the first on the key's unique index.
//
// Both halves are asserted, because either alone would pass on the broken
// code: that the second boot waits at all — observed as its own backend
// holding an ungranted lock, not as elapsed time — and that what it reads
// once released is the version the first recorded rather than nothing.
func TestASecondBootWaitsForTheFirstToRecordItsVersion(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	if _, err := store.pool.Exec(ctx, `DELETE FROM stats_meta WHERE key = 'rollup_version'`); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_, _ = store.pool.Exec(context.Background(),
			`INSERT INTO stats_meta (key, value) VALUES ('rollup_version', $1)
			 ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value`, currentRollupVersion())
	})

	first, err := store.pool.Begin(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer first.Rollback(ctx)
	recorded, err := lockRollupVersion(ctx, first)
	if err != nil {
		t.Fatal(err)
	}
	if recorded != "" {
		t.Fatalf("first boot read %q, want the empty string on a cold database", recorded)
	}

	pids, second := make(chan int32, 1), make(chan string, 1)
	go func() {
		background := context.Background()
		conn, err := store.pool.Acquire(background)
		if err != nil {
			pids <- 0
			second <- "acquire: " + err.Error()
			return
		}
		defer conn.Release()
		var pid int32
		if err := conn.QueryRow(background, "SELECT pg_backend_pid()").Scan(&pid); err != nil {
			pids <- 0
			second <- "pid: " + err.Error()
			return
		}
		pids <- pid
		tx, err := conn.Begin(background)
		if err != nil {
			second <- "begin: " + err.Error()
			return
		}
		defer tx.Rollback(background)
		got, err := lockRollupVersion(background, tx)
		if err != nil {
			second <- "lock: " + err.Error()
			return
		}
		second <- got
	}()

	pid := <-pids
	waited := false
	for deadline := time.Now().Add(20 * time.Second); !waited && time.Now().Before(deadline); {
		var blocked int
		if err := store.pool.QueryRow(ctx,
			"SELECT count(*) FROM pg_locks WHERE pid = $1 AND NOT granted", pid).Scan(&blocked); err != nil {
			t.Fatal(err)
		}
		waited = blocked > 0
		if !waited {
			time.Sleep(20 * time.Millisecond)
		}
	}
	if !waited {
		t.Fatal("the second boot never waited for the first: on a cold database both read no " +
			"version, both drop, and the later drop takes the tables the earlier one filled")
	}

	if _, err := first.Exec(ctx,
		`INSERT INTO stats_meta (key, value) VALUES ('rollup_version', 'first')
		 ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value`); err != nil {
		t.Fatal(err)
	}
	if err := first.Commit(ctx); err != nil {
		t.Fatal(err)
	}

	select {
	case got := <-second:
		if got != "first" {
			t.Errorf("second boot read %q, want the version the first recorded; reading "+
				"anything else is deciding to drop the tables the first just filled", got)
		}
	case <-time.After(30 * time.Second):
		t.Fatal("the second boot never returned")
	}
}

type cell struct{ requests, errors int64 }

// Every service row, keyed by the four columns a reader groups on.
func serviceTotals(ctx context.Context, store *Store) (map[string]cell, error) {
	rows, _, err := store.Services(ctx, 2, 5000)
	if err != nil {
		return nil, err
	}
	out := map[string]cell{}
	for _, r := range rows {
		at := r.Host + " " + r.Service + " " + r.Source + " " + r.AgentClass
		out[at] = cell{out[at].requests + r.Requests, out[at].errors + r.Errors}
	}
	return out, nil
}

// The per-backend read: the two routes that reach one_d4 arrive folded
// into one service, and a route-blind site's unrouted rows arrive named
// rather than pooled with the paths nothing served.
func TestServicesNamesTheBackendBehindEachRouteRow(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)

	rollup := NewRollup()
	row := func(host, route, source string, status int) RequestKey {
		return RequestKey{
			Date: date, Host: host, Route: route, Source: source,
			Status: status, Method: "GET", AgentClass: AgentBrowser, Agent: "",
		}
	}
	rollup.Requests[row("api.muchq.com", "/1d4/v1/query", SourceUI, 200)] = 5
	rollup.Requests[row("api.muchq.com", "/1d4/v1/index", SourceUI, 200)] = 3
	rollup.Requests[row("api.muchq.com", "/1d4/v1/query", SourceAPI, 500)] = 2
	rollup.Requests[row("git.muchq.com", OtherRoute, SourceAPI, 200)] = 7
	rollup.Requests[row("api.muchq.com", OtherRoute, SourceAPI, 404)] = 4
	// The fixture's own host keeps this run's rows tellable from the rest
	// of the shared database.
	rollup.Requests[row(host, OtherRoute, SourceAPI, 200)] = 1
	// The rows below land on sites every other run shares, so the counts
	// are deltas across this one apply rather than totals.
	before, err := serviceTotals(ctx, store)
	if err != nil {
		t.Fatal(err)
	}
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}
	after, err := serviceTotals(ctx, store)
	if err != nil {
		t.Fatal(err)
	}
	got := map[string]cell{}
	for at, cells := range after {
		got[at] = cell{cells.requests - before[at].requests, cells.errors - before[at].errors}
	}
	for at, want := range map[string]cell{
		// 5 + 3, folded across the two routes that reach one_d4. A reader
		// summing per service must not have to know there were two.
		"api.muchq.com one_d4 " + SourceUI + " " + AgentBrowser:                {8, 0},
		"api.muchq.com one_d4 " + SourceAPI + " " + AgentBrowser:               {2, 2},
		"git.muchq.com forgejo " + SourceAPI + " " + AgentBrowser:              {7, 0},
		"api.muchq.com " + OtherService + " " + SourceAPI + " " + AgentBrowser: {4, 4},
		host + " " + OtherService + " " + SourceAPI + " " + AgentBrowser:       {1, 0},
	} {
		if got[at] != want {
			t.Errorf("%s = %+v, want %+v", at, got[at], want)
		}
	}
}

// The limit truncates the busiest-first list and says what it truncated
// from. Both halves are what a reader summing a service depends on: the
// fold happens before this, so what is dropped is whole quiet services
// rather than a slice out of a busy one.
func TestServicesHonoursTheLimitBusiestFirst(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)

	rollup := NewRollup()
	for i, count := range []int64{9, 7, 5} {
		rollup.Requests[RequestKey{
			Date: date, Host: host, Route: OtherRoute, Source: SourceAPI,
			Status: 200, Method: "GET", AgentClass: AgentBrowser,
			Agent: fmt.Sprintf("%s-%d", host, i),
		}] = count
	}
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}

	full, total, err := store.Services(ctx, 2, 5000)
	if err != nil {
		t.Fatal(err)
	}
	if total != len(full) {
		t.Errorf("total = %d with %d rows returned; an untruncated read reports its own length",
			total, len(full))
	}
	if len(full) < 2 {
		t.Fatalf("only %d service rows; the fixture cannot show an ordering", len(full))
	}
	for i := 1; i < len(full); i++ {
		if full[i-1].Requests < full[i].Requests {
			t.Fatalf("row %d (%d requests) precedes row %d (%d); want busiest first",
				i-1, full[i-1].Requests, i, full[i].Requests)
		}
	}

	// The limit cuts the list and leaves the count of what there was. It
	// cuts by service rather than by row, so a limit of one still answers
	// with the busiest service whole — what it must not do is answer with
	// part of one, which TestServicesTruncatesWholeServicesAgainstTheDatabase
	// is what pins.
	cut, total, err := store.Services(ctx, 2, 1)
	if err != nil {
		t.Fatal(err)
	}
	if len(cut) == 0 {
		t.Fatal("limit 1 returned nothing; the busiest service is answered whole or not at all")
	}
	if total != len(full) {
		t.Errorf("truncated total = %d, want the %d there were", total, len(full))
	}
	if cut[0] != full[0] {
		t.Errorf("limit 1 led with %+v, want the busiest row %+v", cut[0], full[0])
	}
}

// Two rows the busiest-first rule cannot separate, across two days.
//
// The date is a column of its own: fold it away and a week of traffic
// reads as one day at seven times the volume. And the comparator's
// tiebreakers are what keep the truncation point still between identical
// reads — with only the request count, two equal rows order by whatever
// the map handed the sort.
func TestServicesKeepsTheDaysApartAndBreaksTiesTheSameWayTwice(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	_, host, key := uniqueFixture(t)
	today := time.Now().UTC().Format("2006-01-02")
	yesterday := time.Now().UTC().AddDate(0, 0, -1).Format("2006-01-02")

	rollup := NewRollup()
	for _, at := range []struct{ date, source string }{
		{today, SourceAPI}, {today, SourceUI}, {yesterday, SourceAPI},
	} {
		rollup.Requests[RequestKey{
			Date: at.date, Host: host, Route: OtherRoute, Source: at.source,
			Status: 200, Method: "GET", AgentClass: AgentBrowser, Agent: "",
		}] = 5
	}
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}

	rows, _, err := store.Services(ctx, 2, 5000)
	if err != nil {
		t.Fatal(err)
	}
	var mine []ServiceRow
	for _, row := range rows {
		if row.Host == host {
			mine = append(mine, row)
		}
	}
	// Three rows, not two: the two days do not merge.
	want := []ServiceRow{
		{Date: today, Host: host, Service: OtherService, Source: SourceAPI,
			AgentClass: AgentBrowser, Requests: 5},
		{Date: today, Host: host, Service: OtherService, Source: SourceUI,
			AgentClass: AgentBrowser, Requests: 5},
		{Date: yesterday, Host: host, Service: OtherService, Source: SourceAPI,
			AgentClass: AgentBrowser, Requests: 5},
	}
	if len(mine) != len(want) {
		t.Fatalf("got %d rows for this run, want %d: %+v", len(mine), len(want), mine)
	}
	// Equal counts, so the order is entirely the tiebreakers: the later
	// day first, then the caller alphabetically.
	for i := range want {
		if mine[i] != want[i] {
			t.Errorf("row %d = %+v, want %+v", i, mine[i], want[i])
		}
	}
}

// Truncation takes whole services, so what a reader sums is exact for
// every service present.
//
// A row is one day of one caller of one class, and a service spans many.
// Cutting the row list instead would leave a service its busy days and
// take its quiet ones, and nothing in the response would say that the
// total under its name is short — which is the shape this replaced.
func TestTruncationTakesWholeServices(t *testing.T) {
	// Busiest first, as Services has already sorted them: big spans three
	// rows, middle two, small one.
	row := func(service string, requests int64) ServiceRow {
		return ServiceRow{
			Date: "2026-08-30", Host: "api.muchq.com", Service: service,
			Source: SourceAPI, AgentClass: AgentBrowser, Requests: requests,
		}
	}
	// noisy has the most rows and the least traffic, so a selection that
	// ranked by row count rather than requests would take it first.
	rows := []ServiceRow{
		row("big", 50), row("big", 30), row("big", 10),
		row("middle", 40), row("middle", 5),
		row("noisy", 3), row("noisy", 3), row("noisy", 2), row("noisy", 1),
		row("small", 20),
	}

	// Four leaves room for big (3) and not middle (2), and stops there
	// rather than reaching past middle for small.
	kept := wholeServicesWithin(rows, 4)
	if len(kept) != 3 {
		t.Fatalf("kept %d rows, want big's three: %+v", len(kept), kept)
	}
	for _, at := range kept {
		if at.Service != "big" {
			t.Errorf("kept %s, want only big", at.Service)
		}
	}

	// Five holds big and middle whole, and noisy's four rows do not buy
	// it a place ahead of them.
	kept = wholeServicesWithin(rows, 5)
	if len(kept) != 5 {
		t.Errorf("limit 5 kept %d rows, want big and middle whole", len(kept))
	}
	for _, at := range kept {
		if at.Service == "noisy" {
			t.Errorf("kept noisy, which has the most rows and the least traffic")
		}
	}

	// A service larger than the whole limit is still answered, because a
	// read that returns nothing is worse than one that returns too much.
	if kept := wholeServicesWithin(rows, 1); len(kept) != 3 {
		t.Errorf("limit 1 kept %d rows, want the busiest service whole", len(kept))
	}

	// And every row of a kept service survives in its original order.
	var order []int64
	for _, at := range kept {
		order = append(order, at.Requests)
	}
	want := []int64{50, 30, 10, 40, 5}
	for i := range want {
		if order[i] != want[i] {
			t.Errorf("kept order = %v, want %v", order, want)
			break
		}
	}
}

// Services itself truncates by service, not just the helper it calls.
//
// Read twice, once past the ceiling and once under it, and compare: every
// service the short read returns must carry all the rows the long read
// gave it. A row-grain slice passes the helper's own test and fails this
// one, which is the regression worth pinning — the page sums these.
func TestServicesTruncatesWholeServicesAgainstTheDatabase(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)

	// Three services, so there is something to drop whatever else the
	// window holds: one_d4 over several rows, forgejo, and this run's own
	// host, which no site serves and so reaches no backend.
	rollup := NewRollup()
	at := func(host, route, source string, requests int64) {
		rollup.Requests[RequestKey{
			Date: date, Host: host, Route: route, Source: source,
			Status: 200, Method: "GET", AgentClass: AgentBrowser, Agent: "",
		}] = requests
	}
	at("api.muchq.com", "/1d4/v1/query", SourceUI, 90)
	at("api.muchq.com", "/1d4/v1/query", SourceAPI, 40)
	at("api.muchq.com", "/1d4/v1/index", SourceAPI, 20)
	at("git.muchq.com", OtherRoute, SourceAPI, 50)
	at(host, OtherRoute, SourceAPI, 5)
	if err := store.ApplyRollup(ctx, key, rollup); err != nil {
		t.Fatal(err)
	}

	full, _, err := store.Services(ctx, 2, 100000)
	if err != nil {
		t.Fatal(err)
	}
	rowsPerService := map[string]int{}
	for _, row := range full {
		rowsPerService[row.Service]++
	}
	if len(rowsPerService) < 2 {
		t.Fatalf("only %d services in the window; this cannot show a cut", len(rowsPerService))
	}

	// A ceiling that has to drop something, whatever else is in the window.
	cut, total, err := store.Services(ctx, 2, len(full)-1)
	if err != nil {
		t.Fatal(err)
	}
	if total != len(full) {
		t.Errorf("total = %d, want the %d rows there were", total, len(full))
	}
	if len(cut) >= len(full) {
		t.Fatalf("a limit below the row count returned %d of %d rows", len(cut), len(full))
	}
	cutPerService := map[string]int{}
	for _, row := range cut {
		cutPerService[row.Service]++
	}
	for service, count := range cutPerService {
		if count != rowsPerService[service] {
			t.Errorf("%s came back with %d of its %d rows; a service is returned whole or "+
				"not at all, or its total is silently short", service, count, rowsPerService[service])
		}
	}
	if len(cutPerService) == 0 {
		t.Error("the truncated read returned no service at all")
	}
}

func TestRollupTablesIsEveryTableTheSchemaCreates(t *testing.T) {
	created := map[string]bool{}
	for _, ddl := range schema {
		for _, match := range regexp.MustCompile(`CREATE TABLE IF NOT EXISTS (\w+)`).
			FindAllStringSubmatch(ddl, -1) {
			created[match[1]] = true
		}
	}
	require.NotEmpty(t, created, "no CREATE TABLE found in schema; has its shape changed?")
	listed := map[string]bool{}
	for _, table := range rollupTables {
		listed[table] = true
	}
	assert.Equal(t, created, listed,
		"rollupTables and schema disagree. A table the schema creates and the list omits "+
			"survives a version bump holding rows keyed the old way.")
}

func TestAVersionBumpDropsAggregatesAndMarkersForReaggregation(t *testing.T) {
	store := testStore(t)
	ctx := context.Background()
	date, host, key := uniqueFixture(t)
	rollup := NewRollup()
	rollup.Requests[RequestKey{Date: date, Host: host, Route: OtherRoute, Source: SourceAPI, Status: 200, Method: "GET", AgentClass: AgentBot, Agent: "curl"}] = 1
	rollup.Slugs[SlugKey{date, host + "-slug", 302}] = 1
	rollup.Probes[ProbeKey{Date: date, Host: host, Route: OtherRoute, Probe: ProbeGit, Status: 404}] = 1
	rollup.Queries[QueryKey{date, host, "ui", "ok", "live"}] = 1
	rollup.Terms[TermKey{date, host, KindField, "eco"}] = 1
	rollup.Countries[GeoKey{date, host, AgentBot, "GB"}] = GeoStat{1, 0, 0}
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
	if row := countryRowFor(t, reopened, host, "GB"); row != nil {
		t.Errorf("geo aggregates survived the version bump: %+v", row)
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
	if rollupVersionFor(RollupVersion+".next", schema) == rollupVersionFor(RollupVersion, schema) {
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
	rollup.Requests[RequestKey{Date: date, Host: host, Route: OtherRoute, Source: SourceAPI, Status: 200, Method: "GET", AgentClass: AgentBot, Agent: "curl"}] = 1
	if err := reopened.ApplyRollup(ctx, key, rollup); err != nil {
		t.Errorf("the new-shape insert failed after the version change: %v", err)
	}
}
