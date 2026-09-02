// log_shipper moves rolled logs to S3 on an interval (#1457): Caddy's
// access logs and one_d4's query events (#1465), each directory under its
// own partition. Configuration is environment-only: S3_BUCKET, S3_REGION,
// AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, LOG_DIRS (label=dir pairs), and
// SHIP_INTERVAL (Go duration, default 1h). Missing required values fail at
// startup — the compose service is profile-gated for exactly that reason.
package main

import (
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	shipper "github.com/muchq/moonbase/domains/platform/apps/log_shipper"
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

	interval := time.Hour
	if raw := os.Getenv("SHIP_INTERVAL"); raw != "" {
		parsed, err := time.ParseDuration(raw)
		if err != nil {
			logger.Error("SHIP_INTERVAL does not parse as a Go duration", "value", raw, "error", err)
			os.Exit(1)
		}
		interval = parsed
	}
	sources, err := shipper.ParseSources(requireEnv(logger, "LOG_DIRS"))
	if err != nil {
		logger.Error("LOG_DIRS is not usable", "error", err)
		os.Exit(1)
	}
	uploader := &s3lite.S3{
		Bucket: requireEnv(logger, "S3_BUCKET"),
		Region: requireEnv(logger, "S3_REGION"),
		Creds: s3lite.Credentials{
			AccessKeyID:     requireEnv(logger, "AWS_ACCESS_KEY_ID"),
			SecretAccessKey: requireEnv(logger, "AWS_SECRET_ACCESS_KEY"),
		},
		Client: &http.Client{Timeout: 2 * time.Minute},
		Now:    time.Now,
	}
	var shippers []*shipper.Shipper
	for _, source := range sources {
		shippers = append(shippers, &shipper.Shipper{
			Dir:    source.Dir,
			Source: source.Label,
			// Rollers write rolled files in place; two minutes of quiet is
			// the proxy for "nobody is still writing this".
			MinAge:   2 * time.Minute,
			Uploader: uploader,
		})
	}

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	logger.Info("log_shipper started", "sources", sources, "interval", interval.String())
	for {
		for _, s := range shippers {
			shipped, skipped, err := s.ShipOnce()
			if err != nil {
				logger.Error("shipping pass finished with failures", "source", s.Source,
					"shipped", shipped, "skipped", skipped, "error", err)
			} else {
				// Logged every pass, even an idle one: a skipped count that never
				// drains past the live log while shipped stays zero is the
				// signature of a roll-name format the pattern no longer matches.
				logger.Info("shipping pass complete", "source", s.Source,
					"shipped", shipped, "skipped", skipped)
			}
		}
		select {
		case <-ticker.C:
		case sig := <-stop:
			logger.Info("shutting down", "signal", sig.String())
			return
		}
	}
}
