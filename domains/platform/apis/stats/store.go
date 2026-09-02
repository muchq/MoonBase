package stats

import (
	"context"
	"crypto/sha256"
	"fmt"
	"strings"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// The stats schema, applied idempotently at boot — the same in-service
// pattern iili uses for its migrations. Aggregates only: the raw lines
// stay in S3, so a schema change is a re-aggregation, never data loss.
var schema = []string{
	`CREATE TABLE IF NOT EXISTS processed_log_objects (
		key text PRIMARY KEY,
		processed_at timestamptz NOT NULL DEFAULT now()
	)`,
	`CREATE TABLE IF NOT EXISTS request_stats (
		dt date NOT NULL,
		host text NOT NULL,
		status int NOT NULL,
		http_method text NOT NULL,
		agent_class text NOT NULL,
		agent text NOT NULL,
		requests bigint NOT NULL,
		PRIMARY KEY (dt, host, status, http_method, agent_class, agent)
	)`,
	`CREATE TABLE IF NOT EXISTS iili_slug_stats (
		dt date NOT NULL,
		slug text NOT NULL,
		status int NOT NULL,
		requests bigint NOT NULL,
		PRIMARY KEY (dt, slug, status)
	)`,
	`CREATE TABLE IF NOT EXISTS probe_stats (
		dt date NOT NULL,
		host text NOT NULL,
		probe text NOT NULL,
		status int NOT NULL,
		requests bigint NOT NULL,
		PRIMARY KEY (dt, host, probe, status)
	)`,
	`CREATE TABLE IF NOT EXISTS query_stats (
		dt date NOT NULL,
		entry text NOT NULL,
		source text NOT NULL,
		outcome text NOT NULL,
		cache text NOT NULL,
		requests bigint NOT NULL,
		PRIMARY KEY (dt, entry, source, outcome, cache)
	)`,
	`CREATE TABLE IF NOT EXISTS query_term_stats (
		dt date NOT NULL,
		entry text NOT NULL,
		kind text NOT NULL,
		term text NOT NULL,
		requests bigint NOT NULL,
		PRIMARY KEY (dt, entry, kind, term)
	)`,
}

// RollupVersion is the meaning of what Consume computes. Bump it when a
// classifier changes what a row means; a change to the tables themselves
// needs no bump, because the recorded version also carries a hash of the
// schema DDL. Either way, at boot a store whose recorded version differs
// drops every aggregate table and every processed marker in one
// transaction, recreates them from the schema above, and the next pass
// recomputes all of it from the raw lines in S3. That is what "a schema
// change is a re-aggregation" costs — one full pass, during which the
// served counts climb back up from zero — and what makes it never data
// loss. Version 2 named agents and added probe_stats.
const RollupVersion = "2"

func rollupVersionFor(meaning string, ddl []string) string {
	sum := sha256.Sum256([]byte(strings.Join(ddl, "\n")))
	return fmt.Sprintf("%s-%x", meaning, sum[:8])
}

func currentRollupVersion() string { return rollupVersionFor(RollupVersion, schema) }

// The tables a re-aggregation rebuilds; every aggregate table in schema
// belongs here, or a version bump leaves it double-counted.
var rollupTables = []string{
	"processed_log_objects", "request_stats", "iili_slug_stats", "probe_stats",
	"query_stats", "query_term_stats",
}

const metaSchema = `CREATE TABLE IF NOT EXISTS stats_meta (
	key text PRIMARY KEY,
	value text NOT NULL
)`

type Store struct {
	pool *pgxpool.Pool
}

func NewStore(ctx context.Context, databaseURL string) (*Store, error) {
	pool, err := pgxpool.New(ctx, databaseURL)
	if err != nil {
		return nil, err
	}
	store := &Store{pool: pool}
	if _, err := pool.Exec(ctx, metaSchema); err != nil {
		pool.Close()
		return nil, fmt.Errorf("applying schema: %w", err)
	}
	if err := store.dropAggregatesOnVersionChange(ctx); err != nil {
		pool.Close()
		return nil, err
	}
	for _, ddl := range schema {
		if _, err := pool.Exec(ctx, ddl); err != nil {
			pool.Close()
			return nil, fmt.Errorf("applying schema: %w", err)
		}
	}
	return store, nil
}

// dropAggregatesOnVersionChange runs before the schema so that a version
// bump which changed a table's columns recreates it: DROP rather than
// TRUNCATE is what makes a column change and a new table the same
// operation. The version row lands in the same transaction, so a crash
// in between leaves either the old tables at the old version or no
// tables at the new one — never new-version tables holding old rows.
func (s *Store) dropAggregatesOnVersionChange(ctx context.Context) error {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return err
	}
	defer tx.Rollback(ctx)

	var recorded string
	err = tx.QueryRow(ctx,
		`SELECT value FROM stats_meta WHERE key = 'rollup_version' FOR UPDATE`).Scan(&recorded)
	if err != nil && err != pgx.ErrNoRows {
		return fmt.Errorf("reading rollup version: %w", err)
	}
	if recorded == currentRollupVersion() {
		return nil
	}
	for _, table := range rollupTables {
		if _, err := tx.Exec(ctx, "DROP TABLE IF EXISTS "+table); err != nil {
			return fmt.Errorf("dropping %s for re-aggregation: %w", table, err)
		}
	}
	if _, err := tx.Exec(ctx,
		`INSERT INTO stats_meta (key, value) VALUES ('rollup_version', $1)
		 ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value`, currentRollupVersion()); err != nil {
		return err
	}
	return tx.Commit(ctx)
}

func (s *Store) Close() { s.pool.Close() }

// Unprocessed filters keys down to the ones no successful ApplyRollup has
// marked yet.
func (s *Store) Unprocessed(ctx context.Context, keys []string) ([]string, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT key FROM processed_log_objects WHERE key = ANY($1)`, keys)
	if err != nil {
		return nil, err
	}
	seen := map[string]bool{}
	var key string
	if _, err := pgx.ForEachRow(rows, []any{&key}, func() error {
		seen[key] = true
		return nil
	}); err != nil {
		return nil, err
	}
	var out []string
	for _, k := range keys {
		if !seen[k] {
			out = append(out, k)
		}
	}
	return out, nil
}

// ApplyRollup writes one object's aggregates and its processed marker in a
// single transaction: a crash between the two re-processes the object, and
// the marker's conflict arm makes a concurrent duplicate a no-op rather
// than a double count.
func (s *Store) ApplyRollup(ctx context.Context, key string, rollup *Rollup) error {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return err
	}
	defer tx.Rollback(ctx)

	tag, err := tx.Exec(ctx,
		`INSERT INTO processed_log_objects (key) VALUES ($1) ON CONFLICT DO NOTHING`, key)
	if err != nil {
		return err
	}
	if tag.RowsAffected() == 0 {
		return nil // someone else already applied this object
	}
	for k, count := range rollup.Requests {
		if _, err := tx.Exec(ctx,
			`INSERT INTO request_stats (dt, host, status, http_method, agent_class, agent, requests)
			 VALUES ($1, $2, $3, $4, $5, $6, $7)
			 ON CONFLICT (dt, host, status, http_method, agent_class, agent)
			 DO UPDATE SET requests = request_stats.requests + EXCLUDED.requests`,
			k.Date, k.Host, k.Status, k.Method, k.AgentClass, k.Agent, count); err != nil {
			return err
		}
	}
	for k, count := range rollup.Slugs {
		if _, err := tx.Exec(ctx,
			`INSERT INTO iili_slug_stats (dt, slug, status, requests)
			 VALUES ($1, $2, $3, $4)
			 ON CONFLICT (dt, slug, status)
			 DO UPDATE SET requests = iili_slug_stats.requests + EXCLUDED.requests`,
			k.Date, k.Slug, k.Status, count); err != nil {
			return err
		}
	}
	for k, count := range rollup.Probes {
		if _, err := tx.Exec(ctx,
			`INSERT INTO probe_stats (dt, host, probe, status, requests)
			 VALUES ($1, $2, $3, $4, $5)
			 ON CONFLICT (dt, host, probe, status)
			 DO UPDATE SET requests = probe_stats.requests + EXCLUDED.requests`,
			k.Date, k.Host, k.Probe, k.Status, count); err != nil {
			return err
		}
	}
	for k, count := range rollup.Queries {
		if _, err := tx.Exec(ctx,
			`INSERT INTO query_stats (dt, entry, source, outcome, cache, requests)
			 VALUES ($1, $2, $3, $4, $5, $6)
			 ON CONFLICT (dt, entry, source, outcome, cache)
			 DO UPDATE SET requests = query_stats.requests + EXCLUDED.requests`,
			k.Date, k.Entry, k.Source, k.Outcome, k.Cache, count); err != nil {
			return err
		}
	}
	for k, count := range rollup.Terms {
		if _, err := tx.Exec(ctx,
			`INSERT INTO query_term_stats (dt, entry, kind, term, requests)
			 VALUES ($1, $2, $3, $4, $5)
			 ON CONFLICT (dt, entry, kind, term)
			 DO UPDATE SET requests = query_term_stats.requests + EXCLUDED.requests`,
			k.Date, k.Entry, k.Kind, k.Term, count); err != nil {
			return err
		}
	}
	return tx.Commit(ctx)
}

type SummaryRow struct {
	Date       string `json:"date"`
	Host       string `json:"host"`
	AgentClass string `json:"agent_class"`
	Requests   int64  `json:"requests"`
	Errors     int64  `json:"errors"`
}

type SlugRow struct {
	Slug     string `json:"slug"`
	Requests int64  `json:"requests"`
}

// AgentRow is one day of one named agent on one host. Blocked is the 403
// count — the signal for whether a scraper backs off after being refused.
// The agent column is the one caller-shaped key in request_stats, so the
// query takes a limit and hands back the busiest rows first: the tail a
// UA-rotating scanner leaves is exactly the part that never makes the cut.
type AgentRow struct {
	Date       string `json:"date"`
	Host       string `json:"host"`
	AgentClass string `json:"agent_class"`
	Agent      string `json:"agent"`
	Requests   int64  `json:"requests"`
	Blocked    int64  `json:"blocked"`
}

// ProbeRow is one scanner family on one host over the window. Served is
// the sub-400 count: a probe that got an answer is the row to look at.
type ProbeRow struct {
	Host     string `json:"host"`
	Probe    string `json:"probe"`
	Requests int64  `json:"requests"`
	Served   int64  `json:"served"`
}

func (s *Store) Summary(ctx context.Context, days int) ([]SummaryRow, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT dt::text, host, agent_class,
		        SUM(requests) AS requests,
		        COALESCE(SUM(requests) FILTER (WHERE status >= 400), 0) AS errors
		 FROM request_stats
		 WHERE dt >= current_date - $1::int
		 GROUP BY dt, host, agent_class
		 ORDER BY dt DESC, host, agent_class`, days)
	if err != nil {
		return nil, err
	}
	var out []SummaryRow
	var row SummaryRow
	_, err = pgx.ForEachRow(rows,
		[]any{&row.Date, &row.Host, &row.AgentClass, &row.Requests, &row.Errors},
		func() error {
			out = append(out, row)
			return nil
		})
	return out, err
}

func (s *Store) TopSlugs(ctx context.Context, days, limit int) ([]SlugRow, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT slug, SUM(requests) AS requests
		 FROM iili_slug_stats
		 WHERE dt >= current_date - $1::int AND status < 400
		 GROUP BY slug
		 ORDER BY requests DESC, slug
		 LIMIT $2`, days, limit)
	if err != nil {
		return nil, err
	}
	var out []SlugRow
	var row SlugRow
	_, err = pgx.ForEachRow(rows, []any{&row.Slug, &row.Requests}, func() error {
		out = append(out, row)
		return nil
	})
	return out, err
}

func (s *Store) Agents(ctx context.Context, days, limit int) ([]AgentRow, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT dt::text, host, agent_class, agent,
		        SUM(requests) AS requests,
		        COALESCE(SUM(requests) FILTER (WHERE status = 403), 0) AS blocked
		 FROM request_stats
		 WHERE dt >= current_date - $1::int
		 GROUP BY dt, host, agent_class, agent
		 ORDER BY requests DESC, dt DESC, host, agent_class, agent
		 LIMIT $2`, days, limit)
	if err != nil {
		return nil, err
	}
	var out []AgentRow
	var row AgentRow
	_, err = pgx.ForEachRow(rows,
		[]any{&row.Date, &row.Host, &row.AgentClass, &row.Agent, &row.Requests, &row.Blocked},
		func() error {
			out = append(out, row)
			return nil
		})
	return out, err
}

func (s *Store) Probes(ctx context.Context, days int) ([]ProbeRow, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT host, probe,
		        SUM(requests) AS requests,
		        COALESCE(SUM(requests) FILTER (WHERE status < 400), 0) AS served
		 FROM probe_stats
		 WHERE dt >= current_date - $1::int
		 GROUP BY host, probe
		 ORDER BY requests DESC, host, probe`, days)
	if err != nil {
		return nil, err
	}
	var out []ProbeRow
	var row ProbeRow
	_, err = pgx.ForEachRow(rows, []any{&row.Host, &row.Probe, &row.Requests, &row.Served},
		func() error {
			out = append(out, row)
			return nil
		})
	return out, err
}

// QueryRow is one day of one_d4 queries by entry, source, outcome, and
// cache. Latency is not here: the tsdb holds the histogram (#1460).
type QueryRow struct {
	Date     string `json:"date"`
	Entry    string `json:"entry"`
	Source   string `json:"source"`
	Outcome  string `json:"outcome"`
	Cache    string `json:"cache"`
	Requests int64  `json:"requests"`
}

// TermRow is one field, motif, order-by motif, or group-by term and how
// many queries of that entry used it over the window.
type TermRow struct {
	Entry    string `json:"entry"`
	Kind     string `json:"kind"`
	Term     string `json:"term"`
	Requests int64  `json:"requests"`
}

func (s *Store) Queries(ctx context.Context, days int) ([]QueryRow, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT dt::text, entry, source, outcome, cache, requests
		 FROM query_stats
		 WHERE dt >= current_date - $1::int
		 ORDER BY dt DESC, entry, source, outcome, cache`, days)
	if err != nil {
		return nil, err
	}
	var out []QueryRow
	var row QueryRow
	_, err = pgx.ForEachRow(rows,
		[]any{&row.Date, &row.Entry, &row.Source, &row.Outcome, &row.Cache, &row.Requests},
		func() error {
			out = append(out, row)
			return nil
		})
	return out, err
}

func (s *Store) QueryTerms(ctx context.Context, days, limit int) ([]TermRow, error) {
	rows, err := s.pool.Query(ctx,
		`SELECT entry, kind, term, SUM(requests) AS requests
		 FROM query_term_stats
		 WHERE dt >= current_date - $1::int
		 GROUP BY entry, kind, term
		 ORDER BY requests DESC, entry, kind, term
		 LIMIT $2`, days, limit)
	if err != nil {
		return nil, err
	}
	var out []TermRow
	var row TermRow
	_, err = pgx.ForEachRow(rows, []any{&row.Entry, &row.Kind, &row.Term, &row.Requests},
		func() error {
			out = append(out, row)
			return nil
		})
	return out, err
}
