package prom_proxy

import (
	"bytes"
	"context"
	"encoding/json"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestExtractFloatValue(t *testing.T) {
	tests := []struct {
		name        string
		result      Result
		expected    float64
		expectError bool
	}{
		{
			name: "ValidValue",
			result: Result{
				Value: []interface{}{1609459200.0, "42.5"},
			},
			expected:    42.5,
			expectError: false,
		},
		{
			name: "InvalidFormat_TooFewElements",
			result: Result{
				Value: []interface{}{1609459200.0},
			},
			expectError: true,
		},
		{
			name: "InvalidFormat_NonStringValue",
			result: Result{
				Value: []interface{}{1609459200.0, 42.5},
			},
			expectError: true,
		},
		{
			name: "InvalidFormat_NonNumericString",
			result: Result{
				Value: []interface{}{1609459200.0, "not_a_number"},
			},
			expectError: true,
		},
		{
			name: "EmptyValue",
			result: Result{
				Value: []interface{}{},
			},
			expectError: true,
		},
		{
			name: "NaNValue",
			result: Result{
				Value: []interface{}{1609459200.0, "NaN"},
			},
			expected:    0,
			expectError: false,
		},
		{
			name: "PositiveInfinityValue",
			result: Result{
				Value: []interface{}{1609459200.0, "+Inf"},
			},
			expected:    0,
			expectError: false,
		},
		{
			name: "NegativeInfinityValue",
			result: Result{
				Value: []interface{}{1609459200.0, "-Inf"},
			},
			expected:    0,
			expectError: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			value, err := extractFloatValue(&tt.result)
			if tt.expectError {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.expected, value)
			}
		})
	}
}

func TestExtractTimeSeries(t *testing.T) {
	tests := []struct {
		name           string
		result         Result
		expectedName   string
		expectedLabels map[string]string
		expectedValues int
		expectError    bool
	}{
		{
			name: "ValidTimeSeries",
			result: Result{
				Metric: map[string]string{
					"__name__": "cpu_utilization",
					"instance": "localhost:9090",
				},
				Values: [][]interface{}{
					{1609459200.0, "25.5"},
					{1609459230.0, "26.1"},
					{1609459260.0, "24.8"},
				},
			},
			expectedName: "cpu_utilization",
			expectedLabels: map[string]string{
				"__name__": "cpu_utilization",
				"instance": "localhost:9090",
			},
			expectedValues: 3,
			expectError:    false,
		},
		{
			name: "NoMetricName",
			result: Result{
				Metric: map[string]string{
					"instance": "localhost:9090",
				},
				Values: [][]interface{}{
					{1609459200.0, "25.5"},
				},
			},
			expectedName: "unnamed_metric",
			expectedLabels: map[string]string{
				"instance": "localhost:9090",
			},
			expectedValues: 1,
			expectError:    false,
		},
		{
			name: "InvalidValueFormat",
			result: Result{
				Metric: map[string]string{
					"__name__": "test_metric",
				},
				Values: [][]interface{}{
					{1609459200.0, "25.5"},
					{1609459230.0},            // Invalid - missing value
					{"not_timestamp", "26.1"}, // Invalid - non-numeric timestamp
					{1609459290.0, 27.5},      // Invalid - non-string value
				},
			},
			expectedName: "test_metric",
			expectedLabels: map[string]string{
				"__name__": "test_metric",
			},
			expectedValues: 1, // Only the first valid value
			expectError:    false,
		},
		{
			name: "EmptyValues",
			result: Result{
				Metric: map[string]string{
					"__name__": "empty_metric",
				},
				Values: [][]interface{}{},
			},
			expectedName: "empty_metric",
			expectedLabels: map[string]string{
				"__name__": "empty_metric",
			},
			expectedValues: 0,
			expectError:    false,
		},
		{
			name: "TimeSeriesWithNaNValues",
			result: Result{
				Metric: map[string]string{
					"__name__": "test_metric_with_nan",
				},
				Values: [][]interface{}{
					{1609459200.0, "25.5"},
					{1609459230.0, "NaN"},
					{1609459260.0, "+Inf"},
					{1609459290.0, "-Inf"},
					{1609459320.0, "30.1"},
				},
			},
			expectedName: "test_metric_with_nan",
			expectedLabels: map[string]string{
				"__name__": "test_metric_with_nan",
			},
			expectedValues: 5, // All values should be included, NaN/Inf converted to 0
			expectError:    false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ts, err := extractTimeSeries(&tt.result)
			if tt.expectError {
				assert.Error(t, err)
				return
			}

			require.NoError(t, err)
			assert.Equal(t, tt.expectedName, ts.MetricName)
			assert.Equal(t, tt.expectedLabels, ts.Labels)
			assert.Len(t, ts.Values, tt.expectedValues)

			// Verify data point structure for first value if exists
			if len(ts.Values) > 0 {
				assert.IsType(t, time.Time{}, ts.Values[0].Timestamp)
				assert.IsType(t, float64(0), ts.Values[0].Value)
			}
		})
	}
}

func TestMetricsHandler_HealthHandler(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/health", nil)
	w := httptest.NewRecorder()

	handler.HealthHandler(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))

	var response map[string]string
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)

	assert.Equal(t, "healthy", response["status"])
	assert.Equal(t, "prometheus-proxy", response["service"])
	assert.NotEmpty(t, response["timestamp"])

	// Verify timestamp is valid RFC3339 format
	_, err = time.Parse(time.RFC3339, response["timestamp"])
	assert.NoError(t, err)
}

// captureLog collects what the standard logger writes while fn runs.
//
// A zero tile means "the query failed" or "the number is zero" and the payload
// cannot tell them apart — the log line is the whole difference, which both
// the fan-out and the handlers say in as many words. Asserting on tiles without
// asserting on this leaves the distinction they describe untested, and since
// runBounded contains panics, it also leaves a panicking query looking exactly
// like a healthy idle service.
func captureLog(t *testing.T, fn func()) string {
	t.Helper()
	var buf bytes.Buffer
	log.SetOutput(&buf)
	t.Cleanup(func() { log.SetOutput(os.Stderr) })
	fn()
	// Safe unsynchronized: log.Logger serializes its writes, and every
	// goroutine runBounded started has finished before fn returns.
	return buf.String()
}

// Mock Prometheus client for testing handlers
type mockPrometheusClient struct {
	queryResponse      *QueryResponse
	queryRangeResponse *QueryResponse
	queryError         error
	queryRangeError    error
	// When set, responses are looked up by exact query string — a miss
	// returns an empty result, so a mis-wired query reads as zero instead
	// of borrowing another query's value.
	queryResponses map[string]*QueryResponse
	// Same for range queries. Without this a handler can build the wrong
	// query — or none at all — and every assertion still passes.
	queryRangeResponses map[string]*QueryResponse
	// Guards the recording fields below. The service handlers fan their
	// queries out across goroutines (#1556), so a mock that appended without
	// it would be a data race — and `go test -race` would fail the suite it
	// was meant to serve. The fixture maps are written before the handler
	// runs and only read after, so they stay outside it.
	mu sync.Mutex
	// Queries with no fixture entry, so a test can prove nothing was
	// silently answered with an empty result.
	misses []string
	// Every instant query issued, so a test can bound the fan-out. Recorded
	// in completion order, which under a concurrent fan-out is not the order
	// the handler built them in: assert on the set, not the sequence.
	instantQueries []string
}

func (m *mockPrometheusClient) record(query string, missed bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.instantQueries = append(m.instantQueries, query)
	if missed {
		m.misses = append(m.misses, query)
	}
}

func (m *mockPrometheusClient) Query(ctx context.Context, query string) (*QueryResponse, error) {
	if m.queryResponses != nil {
		resp, ok := m.queryResponses[query]
		m.record(query, !ok)
		if ok {
			return resp, nil
		}
		return &QueryResponse{}, nil
	}
	m.record(query, false)
	return m.queryResponse, m.queryError
}

func (m *mockPrometheusClient) QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error) {
	if m.queryRangeResponses != nil {
		if resp, ok := m.queryRangeResponses[query]; ok {
			return resp, nil
		}
		m.mu.Lock()
		m.misses = append(m.misses, query)
		m.mu.Unlock()
		return &QueryResponse{}, nil
	}
	return m.queryRangeResponse, m.queryRangeError
}

func TestNewMetricsHandler(t *testing.T) {
	mockClient := &mockPrometheusClient{}

	handler := NewMetricsHandler(mockClient)

	assert.NotNil(t, handler)
	assert.Equal(t, mockClient, handler.promClient)
}

// Interface check to ensure our mock implements the right interface
var _ interface {
	Query(ctx context.Context, query string) (*QueryResponse, error)
	QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error)
} = (*mockPrometheusClient)(nil)

func scalarResponse(value string) *QueryResponse {
	return &QueryResponse{
		Status: "success",
		Data: struct {
			ResultType string   `json:"resultType"`
			Result     []Result `json:"result"`
		}{
			ResultType: "vector",
			Result: []Result{
				{
					Metric: map[string]string{},
					Value:  []interface{}{1609459200.0, value},
				},
			},
		},
	}
}

func TestIsCrashLooping(t *testing.T) {
	tests := []struct {
		name     string
		restarts float64
		uptime   float64
		want     bool
	}{
		// The shape of the OTEL crash-loop: restarting constantly, never up
		// long enough to serve a request, so its app metrics stay flat.
		{"restarting and never up", 47, 8, true},
		{"at the threshold", crashLoopMinRestarts, crashLoopMaxUptime - 1, true},
		// A fresh deploy is young but stable; a rough hour it recovered from
		// has the restarts without the youth. Neither is a crash loop now.
		{"just deployed", 1, 5, false},
		{"recovered after a bad patch", 12, crashLoopMaxUptime + 1, false},
		{"healthy and long lived", 0, 86400, false},
		// Prometheus returns nothing for a container it has never seen, which
		// arrives here as zeroes; absence must not read as failure.
		{"no data", 0, 0, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.want, isCrashLooping(tt.restarts, tt.uptime))
		})
	}
}

func TestFetchContainerMetrics_SurfacesCrashLoop(t *testing.T) {
	mock := &mockPrometheusClient{queryResponses: map[string]*QueryResponse{
		`max by (name, image, container_label_com_docker_compose_service) (container_last_seen)`: {
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "vector",
				Result: []Result{
					{Metric: map[string]string{"name": "posterize"}, Value: []interface{}{1609459200.0, "1"}},
					{Metric: map[string]string{"name": "games_hub"}, Value: []interface{}{1609459200.0, "1"}},
				},
			},
		},
		groupedRestartsQuery: vectorResponse(
			vectorResult("posterize", "47"),
			vectorResult("games_hub", "0"),
		),
		groupedUptimeQuery: vectorResponse(
			vectorResult("posterize", "8"),
			vectorResult("games_hub", "86400"),
		),
	}}
	handler := NewMetricsHandler(mock)

	metrics, err := handler.fetchContainerMetrics(context.Background())
	require.NoError(t, err)
	require.Len(t, metrics.Containers, 2)

	byName := map[string]ContainerStats{}
	for _, c := range metrics.Containers {
		byName[c.Name] = c
	}

	// Crash-looping: restarting constantly, never up long enough to serve.
	assert.Equal(t, 47.0, byName["posterize"].RestartsLastHour)
	assert.Equal(t, 8.0, byName["posterize"].UptimeSeconds)
	assert.True(t, byName["posterize"].CrashLooping)

	// A healthy neighbour must not be tarred by it.
	assert.Equal(t, 0.0, byName["games_hub"].RestartsLastHour)
	assert.False(t, byName["games_hub"].CrashLooping)
}

// ByCore is keyed on cpu alone, and system_cpu_time_seconds_total carries one
// series per (cpu, state) — so a query that does not pick a state writes eight
// values to each key and keeps whichever Prometheus returned last. That read as
// 0.3% per core on a host the scalar put at 11%.
func TestCPUByCoreReportsBusyTimeNotWhicheverStateCameLast(t *testing.T) {
	want := `100-avg without(otel_scope_name,otel_scope_version,otel_scope_schema_url)(rate(system_cpu_time_seconds_total{state="idle"}[5m]))*100`
	mock := &mockPrometheusClient{queryResponses: map[string]*QueryResponse{
		want: {Data: struct {
			ResultType string   `json:"resultType"`
			Result     []Result `json:"result"`
		}{Result: []Result{
			{Metric: map[string]string{"cpu": "cpu0"}, Value: []interface{}{0.0, "11.5"}},
			{Metric: map[string]string{"cpu": "cpu1"}, Value: []interface{}{0.0, "12.5"}},
		}}},
	}}

	metrics, err := NewMetricsHandler(mock).fetchSystemMetrics(context.Background())
	require.NoError(t, err)
	assert.Equal(t, map[string]float64{"cpu0": 11.5, "cpu1": 12.5}, metrics.CPU.ByCore)
}

// The collector stamps otel_scope_version onto every series, so an upgrade
// forks each host metric into two that tile the window rather than overlap.
// A caller that reads one of them charts history up to the upgrade and nothing
// after it.
func TestHostTimeSeriesAggregatesAwayTheCollectorsOwnVersion(t *testing.T) {
	mock := &mockPrometheusClient{queryRangeResponses: map[string]*QueryResponse{}}
	_, err := NewMetricsHandler(mock).fetchSystemMetricsTimeSeries(context.Background(), LastWeek)
	require.NoError(t, err)
	require.NotEmpty(t, mock.misses)

	for _, query := range mock.misses {
		if strings.Contains(query, "avg(rate(system_cpu_time_seconds_total") {
			continue // already aggregated across every label
		}
		assert.Contains(t, query, "without(otel_scope_name,otel_scope_version,otel_scope_schema_url)",
			"%q keeps the collector's version in the series identity, so an upgrade splits its history", query)
	}
}
