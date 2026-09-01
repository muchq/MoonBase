package stats

import (
	"context"
	"fmt"

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
		requests bigint NOT NULL,
		PRIMARY KEY (dt, host, status, http_method, agent_class)
	)`,
	`CREATE TABLE IF NOT EXISTS iili_slug_stats (
		dt date NOT NULL,
		slug text NOT NULL,
		status int NOT NULL,
		requests bigint NOT NULL,
		PRIMARY KEY (dt, slug, status)
	)`,
}

type Store struct {
	pool *pgxpool.Pool
}

func NewStore(ctx context.Context, databaseURL string) (*Store, error) {
	pool, err := pgxpool.New(ctx, databaseURL)
	if err != nil {
		return nil, err
	}
	for _, ddl := range schema {
		if _, err := pool.Exec(ctx, ddl); err != nil {
			pool.Close()
			return nil, fmt.Errorf("applying schema: %w", err)
		}
	}
	return &Store{pool: pool}, nil
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
			`INSERT INTO request_stats (dt, host, status, http_method, agent_class, requests)
			 VALUES ($1, $2, $3, $4, $5, $6)
			 ON CONFLICT (dt, host, status, http_method, agent_class)
			 DO UPDATE SET requests = request_stats.requests + EXCLUDED.requests`,
			k.Date, k.Host, k.Status, k.Method, k.AgentClass, count); err != nil {
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
