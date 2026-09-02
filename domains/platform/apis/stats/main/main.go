// stats serves the aggregates the log pipeline computes (#1460) and runs
// the aggregation loop that computes them: every AGGREGATE_INTERVAL it
// lists the shipped caddy logs in S3, rolls up the new objects, and
// applies them to the stats database in per-object transactions.
package main

import (
	"context"
	"log/slog"
	"net/http"
	"os"
	"time"

	"github.com/muchq/moonbase/domains/platform/apis/stats"
	"github.com/muchq/moonbase/domains/platform/libs/mucks"
	"github.com/muchq/moonbase/domains/platform/libs/s3lite"
)

func requireEnv(logger *slog.Logger, name string) string {
	value := os.Getenv(name)
	if value == "" {
		logger.Error("required environment variable is not set", "name", name)
		os.Exit(1)
	}
	return value
}

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, nil))

	interval := 15 * time.Minute
	if raw := os.Getenv("AGGREGATE_INTERVAL"); raw != "" {
		parsed, err := time.ParseDuration(raw)
		if err != nil {
			logger.Error("AGGREGATE_INTERVAL does not parse as a Go duration", "value", raw, "error", err)
			os.Exit(1)
		}
		interval = parsed
	}
	port := os.Getenv("PORT")
	if port == "" {
		port = "8092"
	}

	store, err := stats.NewStore(context.Background(), requireEnv(logger, "STATS_DB_URL"))
	if err != nil {
		logger.Error("cannot open the stats database", "error", err)
		os.Exit(1)
	}
	defer store.Close()

	objects := &s3lite.S3{
		Bucket: requireEnv(logger, "S3_BUCKET"),
		Region: requireEnv(logger, "S3_REGION"),
		Creds: s3lite.Credentials{
			AccessKeyID:     requireEnv(logger, "AWS_ACCESS_KEY_ID"),
			SecretAccessKey: requireEnv(logger, "AWS_SECRET_ACCESS_KEY"),
		},
		Client: &http.Client{Timeout: 5 * time.Minute},
		Now:    time.Now,
	}
	// The geo database is optional and, when named, required to load: a
	// configured key that fails is a deployment fault, not a reason to
	// silently file every address under "--".
	var geo stats.Locator = stats.NoLocator{}
	if key := os.Getenv("GEO_DB_KEY"); key != "" {
		loaded, skipped, err := stats.LoadGeo(objects, key)
		if err != nil {
			logger.Error("cannot load the geo database", "key", key, "error", err)
			os.Exit(1)
		}
		logger.Info("geo database loaded", "key", key, "skipped_lines", skipped)
		geo = loaded
	} else {
		logger.Warn("GEO_DB_KEY is not set; geo rows will all read " + stats.UnknownCountry)
	}

	aggregator := &stats.Aggregator{
		Objects: objects,
		Store:   store,
		Logger:  logger,
		Geo:     geo,
	}
	go func() {
		ticker := time.NewTicker(interval)
		defer ticker.Stop()
		for {
			processed, err := aggregator.RunOnce(context.Background())
			if err != nil {
				logger.Error("aggregation pass failed", "error", err)
			} else if processed > 0 {
				logger.Info("aggregation pass complete", "objects", processed)
			}
			<-ticker.C
		}
	}()

	handlers := stats.NewHandlers(store, logger)
	router := mucks.NewJsonMucks()
	router.HandleFunc("GET /health", handlers.Health)
	router.HandleFunc("GET /stats/v1/summary", handlers.GetSummary)
	router.HandleFunc("GET /stats/v1/iili/top", handlers.GetTopSlugs)
	router.HandleFunc("GET /stats/v1/agents", handlers.GetAgents)
	router.HandleFunc("GET /stats/v1/probes", handlers.GetProbes)
	router.HandleFunc("GET /stats/v1/one_d4/queries", handlers.GetQueries)
	router.HandleFunc("GET /stats/v1/one_d4/terms", handlers.GetQueryTerms)
	router.HandleFunc("GET /stats/v1/countries", handlers.GetCountries)

	logger.Info("stats started", "port", port, "interval", interval.String())
	if err := http.ListenAndServe(":"+port, router); err != nil {
		logger.Error("server exited", "error", err)
		os.Exit(1)
	}
}
