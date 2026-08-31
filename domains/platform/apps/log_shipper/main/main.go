// log_shipper moves Caddy's rolled access logs to S3 on an interval (#1457).
// Configuration is environment-only: S3_BUCKET, S3_REGION,
// AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, LOG_DIR, LOG_SOURCE, and
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
	source := os.Getenv("LOG_SOURCE")
	if source == "" {
		source = "caddy"
	}

	s := &shipper.Shipper{
		Dir:    requireEnv(logger, "LOG_DIR"),
		Source: source,
		Uploader: &shipper.S3{
			Bucket: requireEnv(logger, "S3_BUCKET"),
			Region: requireEnv(logger, "S3_REGION"),
			Creds: shipper.Credentials{
				AccessKeyID:     requireEnv(logger, "AWS_ACCESS_KEY_ID"),
				SecretAccessKey: requireEnv(logger, "AWS_SECRET_ACCESS_KEY"),
			},
			Client: &http.Client{Timeout: 2 * time.Minute},
			Now:    time.Now,
		},
	}

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGTERM, syscall.SIGINT)
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	logger.Info("log_shipper started", "dir", s.Dir, "source", s.Source, "interval", interval.String())
	for {
		shipped, err := s.ShipOnce()
		if err != nil {
			logger.Error("shipping pass finished with failures", "shipped", shipped, "error", err)
		} else if shipped > 0 {
			logger.Info("shipping pass complete", "shipped", shipped)
		}
		select {
		case <-ticker.C:
		case sig := <-stop:
			logger.Info("shutting down", "signal", sig.String())
			return
		}
	}
}
