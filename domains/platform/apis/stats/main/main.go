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
	// The geo database rides in the image; GEO_DB_PATH points elsewhere or,
	// empty, switches geo off. A file that will not load is logged and the
	// service runs without it — every geo row reads "--" — rather than
	// failing a boot the other endpoints do not depend on.
	geoPath, geoSet := os.LookupEnv("GEO_DB_PATH")
	if !geoSet {
		geoPath = stats.DefaultGeoDBPath
	}
	geo, skipped, err := stats.Locate(geoPath)
	switch {
	case err != nil:
		logger.Error("cannot load the geo database; geo rows will all read "+stats.UnknownCountry,
			"path", geoPath, "error", err)
	case geoPath == "":
		logger.Warn("GEO_DB_PATH is empty; geo rows will all read " + stats.UnknownCountry)
	default:
		logger.Info("geo database loaded", "path", geoPath, "skipped_lines", skipped)
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

	router := stats.NewRouter(stats.NewHandlers(store, logger))

	logger.Info("stats started", "port", port, "interval", interval.String())
	if err := http.ListenAndServe(":"+port, router); err != nil {
		logger.Error("server exited", "error", err)
		os.Exit(1)
	}
}
