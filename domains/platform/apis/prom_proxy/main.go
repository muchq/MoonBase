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

	// Dashboard overhaul (#1199): catalog, merged host page, and generic
	// per-service pages. The per-service scalar/timeseries routes below
	// are legacy and retire once the UI cutover deploys.
	router.HandleFunc("GET /metrics/v1/services", metricsHandler.GetServiceCatalog)
	router.HandleFunc("GET /metrics/v1/host", metricsHandler.GetHostMetrics)
	router.HandleFunc("GET /metrics/v1/host/timeseries/{range}", metricsHandler.GetHostMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/service/{name}", metricsHandler.GetServiceMetrics)
	router.HandleFunc("GET /metrics/v1/service/{name}/timeseries/{range}", metricsHandler.GetServiceMetricsTimeSeries)

	// Metrics endpoints - current point-in-time data
	router.HandleFunc("GET /metrics/v1/scalar/system", metricsHandler.GetSystemMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/portrait", metricsHandler.GetPortraitMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/containers", metricsHandler.GetContainerMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/summary", metricsHandler.GetSummaryMetrics)
	router.HandleFunc("GET /metrics/v1/scalar/golf", metricsHandler.GetGolfMetrics)

	// Timeseries endpoints - historical data with time ranges
	router.HandleFunc("GET /metrics/v1/timeseries/system/{range}", metricsHandler.GetSystemMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/portrait/{range}", metricsHandler.GetPortraitMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/containers/{range}", metricsHandler.GetContainerMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/microgpt/{range}", metricsHandler.GetMicrogptMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/timeseries/golf/{range}", metricsHandler.GetGolfMetricsTimeSeries)

	// MicroGPT endpoints
	router.HandleFunc("GET /metrics/v1/scalar/microgpt", metricsHandler.GetMicrogptMetrics)

	log.Printf("Starting Prometheus proxy server on port %s", port)
	log.Printf("Prometheus backend: %s", prometheusURL)
	log.Fatal(http.ListenAndServe(":"+port, router))
}
