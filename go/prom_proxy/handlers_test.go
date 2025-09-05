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
	mockClient := &mockPrometheusClient{}
	cache := &MetricsCache{}
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
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
	mockClient := &mockPrometheusClient{}
	cache := &MetricsCache{}
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
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
	mockClient := &mockPrometheusClient{}
	cache := &MetricsCache{}
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
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
	mockClient := &mockPrometheusClient{}
	
	// Create cache with test data
	cache := &MetricsCache{
		systemMetrics: &CacheEntry[*SystemMetrics]{
			Data: &SystemMetrics{
				Timestamp: time.Now().UTC(),
				CPU: CPUMetrics{
					Utilization: 42.5,
					ByCore:      map[string]float64{"0": 40.0, "1": 45.0},
				},
				Memory: MemoryMetrics{
					Total:       8589934592, // 8GB
					Used:        4294967296, // 4GB
					Free:        4294967296, // 4GB
					Utilization: 50.0,
				},
			},
			Timestamp: time.Now(),
		},
	}
	
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
	req := httptest.NewRequest("GET", "/api/v1/metrics/system", nil)
	w := httptest.NewRecorder()
	
	handler.GetSystemMetrics(w, req)
	
	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response SystemMetrics
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	// Verify data from cache
	assert.Equal(t, 42.5, response.CPU.Utilization)
	assert.Equal(t, 50.0, response.Memory.Utilization)
	assert.WithinDuration(t, time.Now(), response.Timestamp, 5*time.Second)
}

func TestMetricsHandler_GetSystemMetrics_CacheEmpty(t *testing.T) {
	mockClient := &mockPrometheusClient{}
	cache := &MetricsCache{} // Empty cache
	
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
	req := httptest.NewRequest("GET", "/api/v1/metrics/system", nil)
	w := httptest.NewRecorder()
	
	handler.GetSystemMetrics(w, req)
	
	assert.Equal(t, http.StatusInternalServerError, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response map[string]interface{}
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Equal(t, float64(500), response["status"])
	assert.Contains(t, response["detail"], "not available in cache")
}

func TestMetricsHandler_GetSystemMetrics_PrometheusError(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryError: assert.AnError,
	}
	cache := &MetricsCache{} // Empty cache to simulate unavailable data
	
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
	req := httptest.NewRequest("GET", "/api/v1/metrics/system", nil)
	w := httptest.NewRecorder()
	
	handler.GetSystemMetrics(w, req)
	
	// Should return 503 when cache is empty
	assert.Equal(t, http.StatusInternalServerError, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response map[string]interface{}
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Equal(t, float64(500), response["status"])
	assert.Contains(t, response["detail"], "not available in cache")
}

func TestMetricsHandler_GetSystemMetricsTimeSeries_Success(t *testing.T) {
	mockClient := &mockPrometheusClient{}
	
	// Create cache with test timeseries data
	cache := &MetricsCache{
		systemTimeseries: make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
	}
	cache.systemTimeseries[Last30Minutes] = &CacheEntry[*TimeSeriesResponse]{
		Data: &TimeSeriesResponse{
			TimeRange: "30m",
			Step:      "30s",
			StartTime: time.Now().Add(-30 * time.Minute),
			EndTime:   time.Now(),
			Series: []TimeSeries{
				{
					MetricName: "cpu_utilization",
					Labels:     map[string]string{"instance": "localhost"},
					Values: []DataPoint{
						{Timestamp: time.Now().Add(-20 * time.Minute), Value: 25.5},
						{Timestamp: time.Now().Add(-10 * time.Minute), Value: 26.1},
					},
				},
			},
		},
		Timestamp: time.Now(),
	}
	
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
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
	assert.Equal(t, "cpu_utilization", response.Series[0].MetricName)
	assert.Len(t, response.Series[0].Values, 2)
}

func TestMetricsHandler_GetSummaryMetrics(t *testing.T) {
	mockClient := &mockPrometheusClient{}
	
	// Create cache with both system and portrait metrics
	cache := &MetricsCache{
		systemMetrics: &CacheEntry[*SystemMetrics]{
			Data: &SystemMetrics{
				Timestamp: time.Now().UTC(),
				CPU: CPUMetrics{Utilization: 42.5},
				Memory: MemoryMetrics{Utilization: 50.0},
			},
			Timestamp: time.Now(),
		},
		portraitMetrics: &CacheEntry[*PortraitMetrics]{
			Data: &PortraitMetrics{
				Timestamp: time.Now().UTC(),
				Requests: RequestMetrics{Total: 1000, Rate: 10.5},
				Cache: CacheMetrics{HitRate: 95.2},
			},
			Timestamp: time.Now(),
		},
	}
	
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
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
	
	// Verify the nested structure
	system := response["system"].(map[string]interface{})
	cpu := system["cpu"].(map[string]interface{})
	assert.Equal(t, 42.5, cpu["utilization_percent"])
	
	portrait := response["portrait"].(map[string]interface{})
	requests := portrait["requests"].(map[string]interface{})
	assert.Equal(t, float64(1000), requests["total"])
}

func TestMetricsHandler_CacheStatusHandler(t *testing.T) {
	mockClient := &mockPrometheusClient{}
	
	// Create cache with test data
	cache := &MetricsCache{
		systemMetrics: &CacheEntry[*SystemMetrics]{
			Data:      &SystemMetrics{},
			Timestamp: time.Now().Add(-5 * time.Minute),
		},
		portraitMetrics: &CacheEntry[*PortraitMetrics]{
			Data:      &PortraitMetrics{},
			Timestamp: time.Now().Add(-3 * time.Minute),
		},
		systemTimeseries:   make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
		portraitTimeseries: make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
	}
	
	// Add some timeseries data
	cache.systemTimeseries[Last30Minutes] = &CacheEntry[*TimeSeriesResponse]{
		Data:      &TimeSeriesResponse{},
		Timestamp: time.Now().Add(-2 * time.Minute),
	}
	
	handler := &MetricsHandler{promClient: mockClient, cache: cache}
	
	req := httptest.NewRequest("GET", "/cache/status", nil)
	w := httptest.NewRecorder()
	
	handler.CacheStatusHandler(w, req)
	
	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "application/json; charset=utf-8", w.Header().Get("Content-Type"))
	
	var response map[string]interface{}
	err := json.Unmarshal(w.Body.Bytes(), &response)
	require.NoError(t, err)
	
	assert.Equal(t, "cache-info", response["status"])
	assert.Equal(t, "prometheus-proxy-cache", response["service"])
	assert.Contains(t, response, "timestamp")
	assert.Contains(t, response, "system_metrics_cached_at")
	assert.Contains(t, response, "portrait_metrics_cached_at")
	assert.Contains(t, response, "system_timeseries")
	assert.Contains(t, response, "portrait_timeseries")
}

func TestNewMetricsHandler(t *testing.T) {
	mockClient := &mockPrometheusClient{}
	cache := &MetricsCache{}
	
	handler := NewMetricsHandler(mockClient, cache)
	
	assert.NotNil(t, handler)
	assert.Equal(t, mockClient, handler.promClient)
	assert.Equal(t, cache, handler.cache)
}

// Interface check to ensure our mock implements the right interface
var _ interface {
	Query(ctx context.Context, query string) (*QueryResponse, error)
	QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error)
} = (*mockPrometheusClient)(nil)