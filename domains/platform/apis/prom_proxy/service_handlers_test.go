package prom_proxy

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func rangeResponse(metricName string) *QueryResponse {
	return &QueryResponse{
		Status: "success",
		Data: struct {
			ResultType string   `json:"resultType"`
			Result     []Result `json:"result"`
		}{
			ResultType: "matrix",
			Result: []Result{
				{
					Metric: map[string]string{"__name__": metricName},
					Values: [][]interface{}{
						{1609459200.0, "25.5"},
						{1609459230.0, "26.1"},
					},
				},
			},
		},
	}
}

func TestRegistry_OrderAndEntriesAgree(t *testing.T) {
	assert.Len(t, serviceOrder, len(serviceRegistry))
	for _, name := range serviceOrder {
		_, ok := serviceRegistry[name]
		assert.True(t, ok, "serviceOrder entry %q missing from registry", name)
	}
}

func TestMetricsHandler_GetServiceCatalog(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/services", nil)
	w := httptest.NewRecorder()

	handler.GetServiceCatalog(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var catalog ServiceCatalog
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &catalog))

	require.Len(t, catalog.Services, 3)
	assert.Equal(t, "golf_hub", catalog.Services[0].Name)
	assert.Equal(t, "microgpt-serve", catalog.Services[1].Name)
	assert.Equal(t, "portrait", catalog.Services[2].Name)
	for _, entry := range catalog.Services {
		assert.True(t, entry.HasCustom, entry.Name)
	}

	// The wire field names are the UI contract.
	assert.Contains(t, w.Body.String(), `"has_custom"`)
}

// A few standard queries pinned as literal strings, so a typo in the
// shared instrument names can't hide behind the enumeration below.
func TestStandardQueries_GoldenStrings(t *testing.T) {
	queries := standardScalarQueries("golf_hub")
	var all []string
	for _, q := range queries {
		all = append(all, q.Query)
	}
	assert.Contains(t, all, `sum(rate(http_server_requests_total{service_name="golf_hub"}[5m]))`)
	assert.Contains(t, all,
		`histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name="golf_hub"}[5m])))`)
	assert.Contains(t, all, `sum(http_server_requests_active_gauge{service_name="golf_hub"})`)
}

func TestMetricsHandler_GetServiceMetrics_MapsEveryFieldDistinctly(t *testing.T) {
	// Distinct value per query: a query wired to the wrong standard field
	// or custom descriptor fails loudly. One custom query is deliberately
	// omitted from the mock — its descriptor must still appear, zeroed.
	responses := map[string]*QueryResponse{}
	standard := standardScalarQueries("golf_hub")
	for i, q := range standard {
		responses[q.Query] = golfScalarResponse(fmt.Sprintf("%d", 100+i))
	}
	entry := serviceRegistry["golf_hub"]
	omitted := entry.CustomScalars[len(entry.CustomScalars)-1]
	for i, def := range entry.CustomScalars {
		if def == omitted {
			continue
		}
		responses[def.Query] = golfScalarResponse(fmt.Sprintf("%d", 200+i))
	}

	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryResponses: responses}}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "golf_hub", response.Service)
	assert.WithinDuration(t, time.Now(), response.Timestamp, 5*time.Second)

	// Standard fields in declaration order of standardScalarQueries.
	assert.Equal(t, 100.0, response.Standard.RequestsTotal)
	assert.Equal(t, 101.0, response.Standard.RatePerSec)
	assert.Equal(t, 102.0, response.Standard.SuccessCount5m)
	assert.Equal(t, 103.0, response.Standard.FailureCount5m)
	assert.Equal(t, 104.0, response.Standard.ErrorRatePercent)
	assert.Equal(t, 105.0, response.Standard.AvgDurationMicros)
	assert.Equal(t, 106.0, response.Standard.P95DurationMicros)
	assert.Equal(t, 107.0, response.Standard.ActiveRequests)

	// Custom groups keep registry order and every descriptor is present.
	require.Len(t, response.Custom, 2)
	assert.Equal(t, "Sessions", response.Custom[0].Title)
	assert.Equal(t, "Activity", response.Custom[1].Title)
	assert.Len(t, response.Custom[0].Metrics, 6)
	require.Len(t, response.Custom[1].Metrics, 3)

	assert.Equal(t, CustomMetricValue{Label: "active", Value: 200.0, Unit: "sessions"},
		response.Custom[0].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "commands_per_sec", Value: 206.0, Unit: "/s"},
		response.Custom[1].Metrics[0])
	// The omitted query's descriptor survives with a zero value.
	last := response.Custom[1].Metrics[2]
	assert.Equal(t, omitted.Label, last.Label)
	assert.Equal(t, 0.0, last.Value)
}

func TestMetricsHandler_GetServiceMetrics_PrometheusError(t *testing.T) {
	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryError: assert.AnError}}

	req := httptest.NewRequest("GET", "/metrics/v1/service/portrait", nil)
	req.SetPathValue("name", "portrait")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	// Outage contract: zeroed values behind a stable page shape, never 500.
	assert.Equal(t, http.StatusOK, w.Code)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, 0.0, response.Standard.RatePerSec)
	require.Len(t, response.Custom, 2)
	assert.Equal(t, "Render cache", response.Custom[0].Title)
	assert.Equal(t, "Scene complexity", response.Custom[1].Title)
	for _, group := range response.Custom {
		for _, metric := range group.Metrics {
			assert.Equal(t, 0.0, metric.Value, metric.Label)
		}
	}
}

func TestMetricsHandler_GetServiceMetrics_UnknownService(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/service/nonesuch", nil)
	req.SetPathValue("name", "nonesuch")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)
	assert.Equal(t, http.StatusNotFound, w.Code)

	// Unknown service outranks an invalid range on the timeseries route.
	req = httptest.NewRequest("GET", "/metrics/v1/service/nonesuch/timeseries/bogus", nil)
	req.SetPathValue("name", "nonesuch")
	req.SetPathValue("range", "bogus")
	w = httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusNotFound, w.Code)
}

func TestMetricsHandler_GetServiceMetricsTimeSeries_InvalidRange(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/bogus", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "bogus")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)

	assert.Equal(t, http.StatusBadRequest, w.Code)

	var response map[string]interface{}
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Contains(t, response["detail"], "Invalid time range")
}

func TestMetricsHandler_GetServiceMetricsTimeSeries_StandardPlusCustom(t *testing.T) {
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{queryRangeResponse: rangeResponse("series")},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/30m", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "30m")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "30m", response.TimeRange)

	names := map[string]bool{}
	for _, series := range response.Series {
		names[series.MetricName] = true
	}
	// The five standard series plus golf's six custom series.
	expected := []string{
		"request_rate", "error_rate_percent", "avg_duration_us", "p95_duration_us",
		"active_requests",
		"sessions_active", "session_starts", "command_rate", "event_rate",
		"rejection_rate", "disconnect_rate",
	}
	assert.Len(t, names, len(expected))
	for _, name := range expected {
		assert.True(t, names[name], name)
	}
}

func TestMetricsHandler_GetHostMetrics_Success(t *testing.T) {
	// One constant scalar response everywhere: the container-list query
	// yields a "caddy" row, and every follow-up scalar reads 42.5.
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryResponse: &QueryResponse{
				Status: "success",
				Data: struct {
					ResultType string   `json:"resultType"`
					Result     []Result `json:"result"`
				}{
					ResultType: "vector",
					Result: []Result{
						{
							Metric: map[string]string{"name": "caddy"},
							Value:  []interface{}{1609459200.0, "42.5"},
						},
					},
				},
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/host", nil)
	w := httptest.NewRecorder()

	handler.GetHostMetrics(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var response HostMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.NotNil(t, response.System)
	assert.Equal(t, 42.5, response.System.CPU.Utilization)
	require.Len(t, response.Containers, 1)
	assert.Equal(t, "caddy", response.Containers[0].Name)
	assert.Equal(t, 42.5, response.Containers[0].CPUUsagePercent)
	assert.WithinDuration(t, time.Now(), response.Timestamp, 5*time.Second)
}

func TestMetricsHandler_GetHostMetrics_PrometheusError(t *testing.T) {
	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryError: assert.AnError}}

	req := httptest.NewRequest("GET", "/metrics/v1/host", nil)
	w := httptest.NewRecorder()

	handler.GetHostMetrics(w, req)

	// Zeroed system, empty containers, 200 — the host page renders.
	assert.Equal(t, http.StatusOK, w.Code)

	var response HostMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.NotNil(t, response.System)
	assert.Equal(t, 0.0, response.System.CPU.Utilization)
	assert.Empty(t, response.Containers)
}

func TestMetricsHandler_GetHostMetricsTimeSeries(t *testing.T) {
	req := httptest.NewRequest("GET", "/metrics/v1/host/timeseries/bogus", nil)
	req.SetPathValue("range", "bogus")
	w := httptest.NewRecorder()

	handler := &MetricsHandler{}
	handler.GetHostMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusBadRequest, w.Code)

	handler = &MetricsHandler{
		promClient: &mockPrometheusClient{queryRangeResponse: rangeResponse("series")},
	}
	req = httptest.NewRequest("GET", "/metrics/v1/host/timeseries/30m", nil)
	req.SetPathValue("range", "30m")
	w = httptest.NewRecorder()

	handler.GetHostMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	hostSeries, containerSeries := 0, 0
	names := map[string]bool{}
	for _, series := range response.Series {
		names[series.MetricName] = true
		if strings.HasPrefix(series.MetricName, "container_") {
			containerSeries++
		} else {
			hostSeries++
		}
	}
	// Host series keep their names; container series are namespaced so
	// the merged payload can't collide.
	assert.True(t, names["cpu_utilization"])
	assert.True(t, names["container_cpu_usage"])
	assert.Equal(t, 5, hostSeries)
	assert.Equal(t, 6, containerSeries)
}
