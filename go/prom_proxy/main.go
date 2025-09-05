package main

import (
	"log"
	"net/http"
	"os"
	"time"

	"github.com/muchq/moonbase/go/mucks"
	prom_proxy "github.com/muchq/moonbase/go/prom_proxy_lib"
)

func getEnvWithDefault(key, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

func main() {
	// Configuration from environment variables
	port := getEnvWithDefault("PORT", "8080")
	prometheusURL := getEnvWithDefault("PROMETHEUS_URL", "http://localhost:9090")
	refreshInterval := getEnvWithDefault("CACHE_REFRESH_INTERVAL", "1m")

	// Parse refresh interval
	interval, err := time.ParseDuration(refreshInterval)
	if err != nil {
		log.Fatalf("Invalid cache refresh interval: %v", err)
	}

	// Initialize Prometheus client
	promClient, err := prom_proxy.NewPrometheusClient(prometheusURL)
	if err != nil {
		log.Fatalf("Failed to create Prometheus client: %v", err)
	}

	// Initialize cache with 1-minute refresh interval
	cache := prom_proxy.NewMetricsCache(promClient, interval)
	defer cache.Close()

	// Initialize handlers with cache
	metricsHandler := prom_proxy.NewMetricsHandler(promClient, cache)

	// Setup router with JSON middleware
	router := mucks.NewJsonMucks()

	// Health endpoint
	router.HandleFunc("GET /health", metricsHandler.HealthHandler)
	
	// Cache status endpoint
	router.HandleFunc("GET /cache/status", metricsHandler.CacheStatusHandler)

	// Metrics endpoints - current point-in-time data
	router.HandleFunc("GET /v1/metrics/system", metricsHandler.GetSystemMetrics)
	router.HandleFunc("GET /v1/metrics/portrait", metricsHandler.GetPortraitMetrics)
	router.HandleFunc("GET /v1/metrics/summary", metricsHandler.GetSummaryMetrics)

	// Timeseries endpoints - historical data with time ranges
	router.HandleFunc("GET /v1/timeseries/system/{range}", metricsHandler.GetSystemMetricsTimeSeries)
	router.HandleFunc("GET /v1/timeseries/portrait/{range}", metricsHandler.GetPortraitMetricsTimeSeries)

	log.Printf("Starting Prometheus proxy server on port %s", port)
	log.Printf("Prometheus backend: %s", prometheusURL)
	log.Printf("Cache refresh interval: %s", interval)
	log.Fatal(http.ListenAndServe(":"+port, router))
}
