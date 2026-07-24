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

	// Dashboard routes (#1199): catalog, merged host page, and generic
	// per-service pages driven by the registry.
	router.HandleFunc("GET /metrics/v1/services", metricsHandler.GetServiceCatalog)
	router.HandleFunc("GET /metrics/v1/host", metricsHandler.GetHostMetrics)
	router.HandleFunc("GET /metrics/v1/host/timeseries/{range}", metricsHandler.GetHostMetricsTimeSeries)
	router.HandleFunc("GET /metrics/v1/service/{name}", metricsHandler.GetServiceMetrics)
	router.HandleFunc("GET /metrics/v1/service/{name}/timeseries/{range}", metricsHandler.GetServiceMetricsTimeSeries)

	log.Printf("Starting Prometheus proxy server on port %s", port)
	log.Printf("Prometheus backend: %s", prometheusURL)
	log.Fatal(http.ListenAndServe(":"+port, router))
}
