package prom_proxy

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

// Mock Prometheus client for cache testing
type mockCachePromClient struct {
	mu                      sync.Mutex
	systemMetricsCallCount  int
	portraitMetricsCallCount int
	systemTimeSeriesCallCount map[TimeRange]int
	portraitTimeSeriesCallCount map[TimeRange]int
	queryResponse           *QueryResponse
	queryRangeResponse      *QueryResponse
	queryError              error
	queryRangeError         error
}

func newMockCachePromClient() *mockCachePromClient {
	return &mockCachePromClient{
		systemTimeSeriesCallCount: make(map[TimeRange]int),
		portraitTimeSeriesCallCount: make(map[TimeRange]int),
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
						Value:  []interface{}{float64(time.Now().Unix()), "42.5"},
					},
				},
			},
		},
		queryRangeResponse: &QueryResponse{
			Status: "success",
			Data: struct {
				ResultType string   `json:"resultType"`
				Result     []Result `json:"result"`
			}{
				ResultType: "matrix",
				Result: []Result{
					{
						Metric: map[string]string{"__name__": "test_metric"},
						Values: [][]interface{}{
							{float64(time.Now().Unix()), "25.5"},
							{float64(time.Now().Unix() + 30), "26.1"},
						},
					},
				},
			},
		},
	}
}

func (m *mockCachePromClient) Query(ctx context.Context, query string) (*QueryResponse, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	// Count calls for different metric types based on query patterns
	if query == `100-avg(rate(system_cpu_time_seconds_total{state="idle"}[5m]))*100` {
		m.systemMetricsCallCount++
	} else if query == `trace_requests_total` {
		m.portraitMetricsCallCount++
	}
	
	if m.queryError != nil {
		return nil, m.queryError
	}
	return m.queryResponse, nil
}

func (m *mockCachePromClient) QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	// Determine the step to identify the time range
	var timeRange TimeRange
	switch step {
	case "30s":
		timeRange = Last30Minutes
	case "5m":
		timeRange = LastDay
	case "1h":
		timeRange = LastWeek
	}
	
	// Count calls for different metric types and ranges
	if query == `100-avg(rate(system_cpu_time_seconds_total{state="idle"}[5m]))*100` {
		m.systemTimeSeriesCallCount[timeRange]++
	} else if query == `rate(trace_requests_total[5m])` {
		m.portraitTimeSeriesCallCount[timeRange]++
	}
	
	if m.queryRangeError != nil {
		return nil, m.queryRangeError
	}
	return m.queryRangeResponse, nil
}

func (m *mockCachePromClient) getCallCounts() (int, int, map[TimeRange]int, map[TimeRange]int) {
	m.mu.Lock()
	defer m.mu.Unlock()
	
	return m.systemMetricsCallCount, m.portraitMetricsCallCount, 
		   m.systemTimeSeriesCallCount, m.portraitTimeSeriesCallCount
}

func TestNewMetricsCache(t *testing.T) {
	mockClient := newMockCachePromClient()
	cache := NewMetricsCache(mockClient, 100*time.Millisecond)
	defer cache.Close()
	
	assert.NotNil(t, cache)
	assert.Equal(t, mockClient, cache.promClient)
	assert.Equal(t, 100*time.Millisecond, cache.refreshInterval)
	
	// Verify cache maps are initialized
	assert.NotNil(t, cache.systemTimeseries)
	assert.NotNil(t, cache.portraitTimeseries)
	
	// Verify all time ranges are initialized
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		_, exists := cache.systemTimeseries[tr]
		assert.True(t, exists, "System timeseries should have entry for %s", tr)
		_, exists = cache.portraitTimeseries[tr]
		assert.True(t, exists, "Portrait timeseries should have entry for %s", tr)
	}
}

func TestMetricsCache_InitialPopulation(t *testing.T) {
	mockClient := newMockCachePromClient()
	cache := NewMetricsCache(mockClient, 100*time.Millisecond)
	defer cache.Close()
	
	// Wait for initial population
	time.Sleep(200 * time.Millisecond)
	
	// Verify cache is populated
	systemMetrics := cache.GetSystemMetrics()
	assert.NotNil(t, systemMetrics, "System metrics should be cached")
	
	portraitMetrics := cache.GetPortraitMetrics()
	assert.NotNil(t, portraitMetrics, "Portrait metrics should be cached")
	
	// Verify timeseries are populated for all ranges
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		systemTS := cache.GetSystemTimeseries(tr)
		assert.NotNil(t, systemTS, "System timeseries for %s should be cached", tr)
		
		portraitTS := cache.GetPortraitTimeseries(tr)
		assert.NotNil(t, portraitTS, "Portrait timeseries for %s should be cached", tr)
	}
}

func TestMetricsCache_PeriodicRefresh(t *testing.T) {
	mockClient := newMockCachePromClient()
	refreshInterval := 50 * time.Millisecond
	cache := NewMetricsCache(mockClient, refreshInterval)
	defer cache.Close()
	
	// Wait for multiple refresh cycles
	time.Sleep(150 * time.Millisecond)
	
	// Check that refresh was called multiple times
	systemCalls, portraitCalls, _, _ := mockClient.getCallCounts()
	
	// Should have at least 2 calls (initial + 1 refresh)
	assert.GreaterOrEqual(t, systemCalls, 2, "System metrics should be refreshed multiple times")
	assert.GreaterOrEqual(t, portraitCalls, 2, "Portrait metrics should be refreshed multiple times")
}

func TestMetricsCache_ConcurrentAccess(t *testing.T) {
	mockClient := newMockCachePromClient()
	cache := NewMetricsCache(mockClient, 100*time.Millisecond)
	defer cache.Close()
	
	// Wait for initial population
	time.Sleep(50 * time.Millisecond)
	
	// Perform concurrent reads and writes
	var wg sync.WaitGroup
	numReaders := 10
	numOps := 50
	
	// Start concurrent readers
	for i := 0; i < numReaders; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < numOps; j++ {
				_ = cache.GetSystemMetrics()
				_ = cache.GetPortraitMetrics()
				for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
					_ = cache.GetSystemTimeseries(tr)
					_ = cache.GetPortraitTimeseries(tr)
				}
				time.Sleep(time.Millisecond)
			}
		}()
	}
	
	wg.Wait()
	
	// Ensure no panics occurred and cache is still functional
	assert.NotNil(t, cache.GetSystemMetrics())
	assert.NotNil(t, cache.GetPortraitMetrics())
}

func TestMetricsCache_GetCacheInfo(t *testing.T) {
	mockClient := newMockCachePromClient()
	cache := NewMetricsCache(mockClient, 100*time.Millisecond)
	defer cache.Close()
	
	// Wait for initial population
	time.Sleep(50 * time.Millisecond)
	
	info := cache.GetCacheInfo()
	
	assert.Contains(t, info, "system_metrics_cached_at")
	assert.Contains(t, info, "portrait_metrics_cached_at")
	assert.Contains(t, info, "system_timeseries")
	assert.Contains(t, info, "portrait_timeseries")
	
	// Verify timestamps are recent
	if systemTime, ok := info["system_metrics_cached_at"].(time.Time); ok {
		assert.WithinDuration(t, time.Now(), systemTime, 5*time.Second)
	}
	
	if portraitTime, ok := info["portrait_metrics_cached_at"].(time.Time); ok {
		assert.WithinDuration(t, time.Now(), portraitTime, 5*time.Second)
	}
	
	// Verify timeseries info structure
	if systemTS, ok := info["system_timeseries"].(map[string]interface{}); ok {
		for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
			assert.Contains(t, systemTS, string(tr))
		}
	} else {
		t.Error("system_timeseries should be a map[string]interface{}")
	}
}

func TestMetricsCache_EmptyCache(t *testing.T) {
	// Test cache behavior when no data has been populated yet
	mockClient := newMockCachePromClient()
	
	// Create cache but don't wait for population
	cache := &MetricsCache{
		systemTimeseries:   make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
		portraitTimeseries: make(map[TimeRange]*CacheEntry[*TimeSeriesResponse]),
		promClient:         mockClient,
	}
	
	// All getters should return nil for empty cache
	assert.Nil(t, cache.GetSystemMetrics())
	assert.Nil(t, cache.GetPortraitMetrics())
	
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		assert.Nil(t, cache.GetSystemTimeseries(tr))
		assert.Nil(t, cache.GetPortraitTimeseries(tr))
	}
}

func TestMetricsCache_RefreshFailure(t *testing.T) {
	mockClient := newMockCachePromClient()
	mockClient.queryError = assert.AnError
	mockClient.queryRangeError = assert.AnError
	
	cache := NewMetricsCache(mockClient, 50*time.Millisecond)
	defer cache.Close()
	
	// Wait for refresh attempts
	time.Sleep(100 * time.Millisecond)
	
	// Cache should handle errors gracefully and not crash
	// When queries fail, empty structs are created rather than nil
	systemMetrics := cache.GetSystemMetrics()
	assert.NotNil(t, systemMetrics, "System metrics should not be nil even with errors")
	assert.Equal(t, 0.0, systemMetrics.CPU.Utilization, "CPU utilization should be 0 on error")
	
	portraitMetrics := cache.GetPortraitMetrics()
	assert.NotNil(t, portraitMetrics, "Portrait metrics should not be nil even with errors")
	assert.Equal(t, 0.0, portraitMetrics.Requests.Total, "Request total should be 0 on error")
	
	// Timeseries should be available but empty
	for _, tr := range []TimeRange{Last30Minutes, LastDay, LastWeek} {
		systemTS := cache.GetSystemTimeseries(tr)
		assert.NotNil(t, systemTS, "System timeseries for %s should not be nil", tr)
		assert.Empty(t, systemTS.Series, "System timeseries should be empty on error")
		
		portraitTS := cache.GetPortraitTimeseries(tr)
		assert.NotNil(t, portraitTS, "Portrait timeseries for %s should not be nil", tr)
		assert.Empty(t, portraitTS.Series, "Portrait timeseries should be empty on error")
	}
}

func TestMetricsCache_Close(t *testing.T) {
	mockClient := newMockCachePromClient()
	cache := NewMetricsCache(mockClient, 20*time.Millisecond)
	
	// Let it run for a bit to ensure it's running
	time.Sleep(100 * time.Millisecond)
	
	// Close the cache
	cache.Close()
	
	// Get call count just after close
	initialSystemCalls, initialPortraitCalls, _, _ := mockClient.getCallCounts()
	
	// Wait some more time - calls should not increase after close
	time.Sleep(100 * time.Millisecond)
	
	finalSystemCalls, finalPortraitCalls, _, _ := mockClient.getCallCounts()
	
	// Call counts should not increase significantly after close 
	// (allow for at most 1 more call that might have been in progress)
	assert.LessOrEqual(t, finalSystemCalls-initialSystemCalls, 1, "No significant new system calls after close")
	assert.LessOrEqual(t, finalPortraitCalls-initialPortraitCalls, 1, "No significant new portrait calls after close")
}