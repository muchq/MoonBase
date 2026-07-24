package prom_proxy

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
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
					"__name__":  "cpu_utilization",
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
				"__name__":  "cpu_utilization",
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
					{1609459230.0}, // Invalid - missing value
					{"not_timestamp", "26.1"}, // Invalid - non-numeric timestamp
					{1609459290.0, 27.5}, // Invalid - non-string value
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
}

func (m *mockPrometheusClient) Query(ctx context.Context, query string) (*QueryResponse, error) {
	if m.queryResponses != nil {
		if resp, ok := m.queryResponses[query]; ok {
			return resp, nil
		}
		return &QueryResponse{}, nil
	}
	return m.queryResponse, m.queryError
}

func (m *mockPrometheusClient) QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error) {
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
		`count by (name, image) (container_last_seen)`: {
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "vector",
				Result: []Result{
					{Metric: map[string]string{"name": "posterize"}, Value: []interface{}{1609459200.0, "1"}},
					{Metric: map[string]string{"name": "golf_hub"}, Value: []interface{}{1609459200.0, "1"}},
				},
			},
		},
		`changes(container_start_time_seconds{name="posterize"}[1h])`: scalarResponse("47"),
		`time()-container_start_time_seconds{name="posterize"}`:       scalarResponse("8"),
		`changes(container_start_time_seconds{name="golf_hub"}[1h])`:  scalarResponse("0"),
		`time()-container_start_time_seconds{name="golf_hub"}`:        scalarResponse("86400"),
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
	assert.Equal(t, 0.0, byName["golf_hub"].RestartsLastHour)
	assert.False(t, byName["golf_hub"].CrashLooping)
}
