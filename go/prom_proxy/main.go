package main

import (
	"log"
	"net/http"
	"os"

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

	// Initialize Prometheus client
	promClient, err := prom_proxy.NewPrometheusClient(prometheusURL)
	if err != nil {
		log.Fatalf("Failed to create Prometheus client: %v", err)
	}

	// Initialize handlers
	metricsHandler := prom_proxy.NewMetricsHandler(promClient)

	// Setup router with JSON middleware
	router := mucks.NewJsonMucks()

	// Health endpoint
	router.HandleFunc("GET /health", metricsHandler.HealthHandler)

	// Metrics endpoints - current point-in-time data
	router.HandleFunc("GET /v1/metrics/system", metricsHandler.GetSystemMetrics)
	router.HandleFunc("GET /v1/metrics/portrait", metricsHandler.GetPortraitMetrics)
	router.HandleFunc("GET /v1/metrics/summary", metricsHandler.GetSummaryMetrics)

	// Timeseries endpoints - historical data with time ranges
	router.HandleFunc("GET /v1/timeseries/system/{range}", metricsHandler.GetSystemMetricsTimeSeries)
	router.HandleFunc("GET /v1/timeseries/portrait/{range}", metricsHandler.GetPortraitMetricsTimeSeries)

	log.Printf("Starting Prometheus proxy server on port %s", port)
	log.Printf("Prometheus backend: %s", prometheusURL)
	log.Fatal(http.ListenAndServe(":"+port, router))
}
