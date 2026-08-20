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

func TestMetricsHandler_GetServiceCatalog(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/services", nil)
	w := httptest.NewRecorder()

	handler.GetServiceCatalog(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var catalog ServiceCatalog
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &catalog))

	// Every service has a custom set now: at minimum the standard Probes tile
	// that charts the /health traffic probeFilter subtracts (#1307).
	wantCustom := []struct {
		name      string
		hasCustom bool
	}{
		{"golf_hub", true},
		{"mcpserver", true},
		{"microgpt-serve", true},
		{"mithril", true},
		{"one_d4", true},
		{"one_d4_v2", true},
		{"portrait", true},
		{"posterize", true},
	}
	require.Len(t, catalog.Services, len(wantCustom))
	for i, want := range wantCustom {
		assert.Equal(t, want.name, catalog.Services[i].Name)
		assert.Equal(t, want.hasCustom, catalog.Services[i].HasCustom, want.name)
	}

	// The wire field names are the UI contract.
	assert.Contains(t, w.Body.String(), `"has_custom"`)
}

func TestMetricsHandler_GetServiceMetrics_MapsEveryFieldDistinctly(t *testing.T) {
	// Distinct value per query: a query wired to the wrong standard field
	// or custom descriptor fails loudly. One custom query is deliberately
	// omitted from the mock — its descriptor must still appear, zeroed.
	responses := map[string]*QueryResponse{}
	standard := standardScalarQueries("golf_hub")
	for i, q := range standard {
		responses[q.Query] = scalarResponse(fmt.Sprintf("%d", 100+i))
	}
	entry := serviceRegistry["golf_hub"]
	omitted := entry.CustomScalars[len(entry.CustomScalars)-1]
	for i, def := range entry.CustomScalars {
		if def == omitted {
			continue
		}
		responses[def.QueryFor(DefaultView)] = scalarResponse(fmt.Sprintf("%d", 200+i))
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

	// An unasked-for view is the default, and it is stated rather than left
	// for the client to assume.
	assert.Equal(t, string(DefaultView), response.View)

	// Custom groups keep registry order and every descriptor is present.
	require.Len(t, response.Custom, 4)
	assert.Equal(t, "Probes", response.Custom[0].Title)
	assert.Equal(t, "Sessions", response.Custom[1].Title)
	assert.Equal(t, "Activity", response.Custom[2].Title)
	assert.Equal(t, "Chat", response.Custom[3].Title)
	assert.Len(t, response.Custom[0].Metrics, 1)
	assert.Len(t, response.Custom[1].Metrics, 7)
	assert.Len(t, response.Custom[2].Metrics, 4)
	require.Len(t, response.Custom[3].Metrics, 5)

	assert.Equal(t, CustomMetricValue{Label: "health_checks", Value: 200.0, Toggleable: true},
		response.Custom[0].Metrics[0])
	// A gauge: one form, so no toggle offered.
	assert.Equal(t, CustomMetricValue{Label: "active", Value: 201.0, Unit: "sessions"},
		response.Custom[1].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "commands", Value: 208.0, Toggleable: true},
		response.Custom[2].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "rejections", Value: 210.0, Toggleable: true},
		response.Custom[2].Metrics[2])
	// Chat starts at CustomScalars index 12, so its first value is 200+12.
	assert.Equal(t, CustomMetricValue{Label: "messages", Value: 212.0, Toggleable: true},
		response.Custom[3].Metrics[0])
	// The omitted query's descriptor survives with a zero value.
	last := response.Custom[3].Metrics[4]
	assert.Equal(t, omitted.Label, last.Label)
	assert.Equal(t, 0.0, last.Value)
}

func TestMetricsHandler_GetServiceMetrics_NoCustomServiceKeepsEmptyArray(t *testing.T) {
	// Every real registry entry now carries at least the standard Probes tile
	// (#1307), so the no-custom-tiles wire shape needs a fixture entry. The
	// contract it pins is unchanged: a service with nothing custom answers
	// `[]`, never null.
	serviceRegistry["fixture_svc"] = serviceEntry{}
	defer delete(serviceRegistry, "fixture_svc")

	mockClient := &mockPrometheusClient{
		queryResponses: map[string]*QueryResponse{
			`sum(rate(http_server_requests_total{service_name="fixture_svc",route!="/health"}[5m]))`: scalarResponse("2.5"),
		},
	}

	handler := &MetricsHandler{promClient: mockClient}

	req := httptest.NewRequest("GET", "/metrics/v1/service/fixture_svc", nil)
	req.SetPathValue("name", "fixture_svc")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	// The UI iterates custom unconditionally: the wire value must be [],
	// never null. Assert on the raw body — unmarshalling erases the
	// distinction.
	assert.Contains(t, w.Body.String(), `"custom":[]`)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, 2.5, response.Standard.RatePerSec)
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
	require.Len(t, response.Custom, 3)
	assert.Equal(t, "Probes", response.Custom[0].Title)
	assert.Equal(t, "Render cache", response.Custom[1].Title)
	assert.Equal(t, "Scene complexity", response.Custom[2].Title)
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
	previous := ""
	for _, series := range response.Series {
		names[series.MetricName] = true
		// Deterministic payload order: the UI renders custom series in
		// payload order, so a map-iteration shuffle would reshuffle its
		// charts on every refresh.
		assert.GreaterOrEqual(t, series.MetricName, previous)
		previous = series.MetricName
	}
	// The seven standard series plus golf's nineteen custom panels: three
	// fixed-form charts, and eight toggleable ones each expanding to a
	// _rate/_count pair.
	expected := []string{
		"request_rate", "request_count", "error_rate_percent", "error_count",
		"avg_duration_us", "p95_duration_us", "active_requests",
		"sessions_active", "session_starts", "chat_catch_up_rows",
		"command_rate", "command_count",
		"event_rate", "event_count",
		"rejection_rate", "rejection_count",
		"disconnect_rate", "disconnect_count",
		"rate_limited_rate", "rate_limited_count",
		"chat_message_rate", "chat_message_count",
		"chat_delivery_rate", "chat_delivery_count",
		"chat_failure_rate", "chat_failure_count",
	}
	assert.Len(t, names, len(expected))
	for _, name := range expected {
		assert.True(t, names[name], name)
	}
}

// End to end through the handler: the 7d range's avg/p95 latency queries
// carry the widened 1h0m15s window, and the mock only answers that exact
// query string — a window that stayed hardcoded at 5m, or that widened to
// bare step without the scrape-interval overlap, would miss the mock's
// fixture and the series would come back empty.
func TestMetricsHandler_GetServiceMetricsTimeSeries_P95LatencyWidensWithTheRangesStep(t *testing.T) {
	p95Query := `histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name="posterize",route!="/health"}[1h0m15s])))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				p95Query: rangeResponse("p95_duration_us"),
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/posterize/timeseries/7d", nil)
	req.SetPathValue("name", "posterize")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var p95 *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "p95_duration_us" {
			p95 = &response.Series[i]
		}
	}
	require.NotNil(t, p95, "p95_duration_us missing from the response")
	require.Len(t, p95.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
}

func TestMetricsHandler_GetServiceMetricsTimeSeries_RequestCountBucketsByTheRangesStep(t *testing.T) {
	countQuery := `sum(increase(http_server_requests_total{service_name="golf_hub",route!="/health"}[1h]))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				countQuery: rangeResponse("request_count"),
			},
		},
	}

	// The 7d range steps at 1h (models.go's GetTimeRangeConfig), which
	// request_count's window has to match — a mismatch here means the mock's
	// exact-string lookup misses and the series comes back empty, catching a
	// step that was hardcoded instead of threaded through.
	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/7d", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var requestCount *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "request_count" {
			requestCount = &response.Series[i]
		}
	}
	require.NotNil(t, requestCount, "request_count missing from the response")
	require.Len(t, requestCount.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
}

// The failure-counter analogue of the request_count test above: error_count
// windows by the range's own step too, and it's a different selector
// (http_server_requests_failure_total, not _total) from request_count, so
// a copy-paste that left it reading the wrong counter would still pass a
// test that only checked the window.
func TestMetricsHandler_GetServiceMetricsTimeSeries_ErrorCountBucketsByTheRangesStep(t *testing.T) {
	countQuery := `sum(increase(http_server_requests_failure_total{service_name="golf_hub",route!="/health"}[1h]))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				countQuery: rangeResponse("error_count"),
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/7d", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var errorCount *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "error_count" {
			errorCount = &response.Series[i]
		}
	}
	require.NotNil(t, errorCount, "error_count missing from the response")
	require.Len(t, errorCount.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
}

// No ?view= on this route at all: request_rate and request_count both come
// back on every request, unconditionally, regardless of query parameters —
// there is nothing here to reject as invalid.
func TestMetricsHandler_GetServiceMetricsTimeSeries_IgnoresViewParam(t *testing.T) {
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{queryRangeResponse: rangeResponse("series")},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/1d?view=cumulative", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "1d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	names := map[string]bool{}
	for _, series := range response.Series {
		names[series.MetricName] = true
	}
	assert.True(t, names["request_rate"])
	assert.True(t, names["request_count"])
}

// End to end through the handler: a toggleable custom chart's two panels
// both land in the response, and the count form is bucketed by the range's
// own step exactly like request_count.
func TestMetricsHandler_GetServiceMetricsTimeSeries_CustomCounterPanelBucketsByStep(t *testing.T) {
	countQuery := `sum(increase(stream_commands_total[1h]))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				countQuery: rangeResponse("command_count"),
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/7d", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var commandCount *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "command_count" {
			commandCount = &response.Series[i]
		}
	}
	require.NotNil(t, commandCount, "command_count missing from the response")
	require.Len(t, commandCount.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
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
	// Restart history is what makes a crash loop visible after the fact.
	assert.True(t, names["container_restarts"])
	assert.Equal(t, 5, hostSeries)
	assert.Equal(t, 7, containerSeries)
}

// --- The count/rate toggle (#1287) ------------------------------------------

func TestMetricsHandler_GetServiceMetrics_RateViewSelectsTheRateForm(t *testing.T) {
	entry := serviceRegistry["golf_hub"]
	responses := map[string]*QueryResponse{}
	for i, def := range entry.CustomScalars {
		responses[def.QueryFor(ViewRate)] = scalarResponse(fmt.Sprintf("%d", 300+i))
	}

	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryResponses: responses}}
	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub?view=rate", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "rate", response.View)

	byLabel := map[string]CustomMetricValue{}
	for _, group := range response.Custom {
		for _, metric := range group.Metrics {
			byLabel[metric.Label] = metric
		}
	}

	// A unitless counter reads per-second; one with a unit keeps it and gains
	// the suffix. Both come from the same descriptor, which in the count view
	// answers "" and "rows".
	assert.Equal(t, "/s", byLabel["commands"].Unit)
	assert.Equal(t, "rows/s", byLabel["delivered_rows"].Unit)
	assert.Equal(t, 308.0, byLabel["commands"].Value)

	// The fixed-form tiles are untouched by the view: a gauge has no rate, and
	// the windowed mean is already a ratio of two rates.
	assert.Equal(t, "sessions", byLabel["active"].Unit)
	assert.False(t, byLabel["active"].Toggleable)
	assert.Equal(t, "rows", byLabel["catch_up_rows_avg_5m"].Unit)
	assert.False(t, byLabel["catch_up_rows_avg_5m"].Toggleable)
}

func TestMetricsHandler_GetServiceMetrics_InvalidViewIsRejected(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub?view=cumulative", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	// Rejected rather than quietly defaulted. "cumulative" is the specific
	// wrong answer someone will reach for, and defaulting would hand them a
	// windowed count while they believed they were reading a lifetime total.
	assert.Equal(t, http.StatusBadRequest, w.Code)

	var response map[string]interface{}
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Contains(t, response["detail"], "Invalid view")
}

// The two new JSON keys, asserted on the raw payload rather than through the
// struct.
//
// Every other handler test unmarshals into ServiceMetricsResponse, which means
// it reads the keys back through the same tags it wrote them with — renaming
// `json:"view"` to `json:"metric_view"` would keep all of them green while the
// UI, which lives in another repo and reads by name, silently loses the field.
// The same reasoning already covers requests_total's *value*; these are the
// keys it applies to.
func TestMetricsHandler_GetServiceMetrics_JsonKeysAreStable(t *testing.T) {
	entry := serviceRegistry["golf_hub"]
	responses := map[string]*QueryResponse{}
	for i, def := range entry.CustomScalars {
		responses[def.QueryFor(DefaultView)] = scalarResponse(fmt.Sprintf("%d", 400+i))
	}

	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryResponses: responses}}
	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)
	require.Equal(t, http.StatusOK, w.Code)

	var raw map[string]interface{}
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &raw))
	assert.Equal(t, "count", raw["view"], `the response must carry a top-level "view" key`)

	groups, ok := raw["custom"].([]interface{})
	require.True(t, ok, "custom is not an array")
	require.NotEmpty(t, groups)

	tiles := map[string]map[string]interface{}{}
	for _, group := range groups {
		for _, tile := range group.(map[string]interface{})["metrics"].([]interface{}) {
			metric := tile.(map[string]interface{})
			tiles[metric["label"].(string)] = metric
		}
	}

	counter, ok := tiles["commands"]
	require.True(t, ok, "golf_hub lost its commands tile")
	assert.Equal(t, true, counter["toggleable"],
		`a counter tile must carry "toggleable": true for the UI to offer the switch`)

	// And the negative half: omitempty means a fixed-form tile has no key at
	// all, which is what tells the UI not to draw a toggle it cannot honour.
	gauge, ok := tiles["active"]
	require.True(t, ok, "golf_hub lost its active tile")
	_, present := gauge["toggleable"]
	assert.False(t, present,
		"a fixed-form tile must omit the key entirely, not send false: %v", gauge)
}
