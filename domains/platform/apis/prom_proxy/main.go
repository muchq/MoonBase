package main

import (
	"log"
	"net/http"
	"os"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
	"github.com/muchq/moonbase/domains/platform/apis/prom_proxy"
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
	router.HandleFunc("GET /metrics/v1/scalar/system", metricsHandler.GetSystemMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/portrait", metricsHandler.GetPortraitMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/containers", metricsHandler.GetContainerMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/summary", metricsHandler.GetSummaryMetrics)

	// Timeseries endpoints - historical data with time ranges
	router.HandleFunc("GET /metrics/v1/timeseries/system/{range}", metricsHandler.GetSystemMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/portrait/{range}", metricsHandler.GetPortraitMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/containers/{range}", metricsHandler.GetContainerMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/microgpt/{range}", metricsHandler.GetMicrogptMetricsTimeSeries)

	// MicroGPT endpoints
	router.HandleFunc("GET /metrics/v1/scalar/microgpt", metricsHandler.GetMicrogptMetrics)

	log.Printf("Starting Prometheus proxy server on port %s", port)
	log.Printf("Prometheus backend: %s", prometheusURL)
	log.Fatal(http.ListenAndServe(":"+port, router))
}
