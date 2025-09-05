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

func TestMetricsHandler_GetSystemMetricsTimeSeries_InvalidRange(t *testing.T) {
	handler := &MetricsHandler{}
	
	// Create a request with invalid range
	req := httptest.NewRequest("GET", "/api/v1/timeseries/system/invalid", nil)
	req.SetPathValue("range", "invalid")
	w := httptest.NewRecorder()
	
	handler.GetSystemMetricsTimeSeries(w, req)
	
	assert.Equal(t, http.StatusBadRequest, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response map[string]interface{}
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Equal(t, float64(400), response["status"])
	assert.Contains(t, response["detail"], "Invalid time range")
}

func TestMetricsHandler_GetPortraitMetricsTimeSeries_InvalidRange(t *testing.T) {
	handler := &MetricsHandler{}
	
	// Create a request with invalid range
	req := httptest.NewRequest("GET", "/api/v1/timeseries/portrait/2h", nil)
	req.SetPathValue("range", "2h")
	w := httptest.NewRecorder()
	
	handler.GetPortraitMetricsTimeSeries(w, req)
	
	assert.Equal(t, http.StatusBadRequest, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response map[string]interface{}
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Equal(t, float64(400), response["status"])
	assert.Contains(t, response["detail"], "Invalid time range")
}

// Mock Prometheus client for testing handlers
type mockPrometheusClient struct {
	queryResponse      *QueryResponse
	queryRangeResponse *QueryResponse
	queryError         error
	queryRangeError    error
}

func (m *mockPrometheusClient) Query(ctx context.Context, query string) (*QueryResponse, error) {
	return m.queryResponse, m.queryError
}

func (m *mockPrometheusClient) QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error) {
	return m.queryRangeResponse, m.queryRangeError
}

func TestMetricsHandler_GetSystemMetrics_Success(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryResponse: &QueryResponse{
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "vector",
				Result: []Result{
					{
						Metric: map[string]string{"__name__": "test_metric"},
						Value:  []interface{}{1609459200.0, "42.5"},
					},
				},
			},
		},
		queryError: nil,
	}
	
	handler := &MetricsHandler{promClient: mockClient}
	
	req := httptest.NewRequest("GET", "/api/v1/metrics/system", nil)
	w := httptest.NewRecorder()
	
	handler.GetSystemMetrics(w, req)
	
	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response SystemMetrics
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	// Verify timestamp is recent
	assert.WithinDuration(t, time.Now(), response.Timestamp, 5*time.Second)
}

func TestMetricsHandler_GetSystemMetrics_PrometheusError(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryError: assert.AnError,
	}
	
	handler := &MetricsHandler{promClient: mockClient}
	
	req := httptest.NewRequest("GET", "/api/v1/metrics/system", nil)
	w := httptest.NewRecorder()
	
	handler.GetSystemMetrics(w, req)
	
	// The handler doesn't fail when individual queries fail, it returns empty metrics
	// This is by design to be resilient to partial Prometheus failures
	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response SystemMetrics
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	// All metric values should be zero due to query failures
	assert.Equal(t, 0.0, response.CPU.Utilization)
	assert.Empty(t, response.CPU.ByCore)
	assert.Equal(t, 0.0, response.Memory.Total)
}

func TestMetricsHandler_GetSystemMetricsTimeSeries_Success(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryRangeResponse: &QueryResponse{
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "matrix",
				Result: []Result{
					{
						Metric: map[string]string{"__name__": "cpu_utilization"},
						Values: [][]interface{}{
							{1609459200.0, "25.5"},
							{1609459230.0, "26.1"},
						},
					},
				},
			},
		},
		queryRangeError: nil,
	}
	
	handler := &MetricsHandler{promClient: mockClient}
	
	req := httptest.NewRequest("GET", "/api/v1/timeseries/system/30m", nil)
	req.SetPathValue("range", "30m")
	w := httptest.NewRecorder()
	
	handler.GetSystemMetricsTimeSeries(w, req)
	
	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response TimeSeriesResponse
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Equal(t, "30m", response.TimeRange)
	assert.Equal(t, "30s", response.Step)
	assert.NotEmpty(t, response.Series)
}

func TestMetricsHandler_GetSummaryMetrics(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryResponse: &QueryResponse{
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "vector",
				Result:     []Result{},
			},
		},
		queryError: nil,
	}
	
	handler := &MetricsHandler{promClient: mockClient}
	
	req := httptest.NewRequest("GET", "/api/v1/metrics/summary", nil)
	w := httptest.NewRecorder()
	
	handler.GetSummaryMetrics(w, req)
	
	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response map[string]interface{}
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Contains(t, response, "timestamp")
	assert.Contains(t, response, "system")
	assert.Contains(t, response, "portrait")
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