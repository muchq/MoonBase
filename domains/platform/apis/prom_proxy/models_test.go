package prom_proxy

import (
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestGetTimeRangeConfig(t *testing.T) {
	tests := []struct {
		name         string
		timeRange    TimeRange
		expectedDur  time.Duration
		expectedStep string
	}{
		{
			name:         "Last30Minutes",
			timeRange:    Last30Minutes,
			expectedDur:  30 * time.Minute,
			expectedStep: "30s",
		},
		{
			name:         "LastDay",
			timeRange:    LastDay,
			expectedDur:  24 * time.Hour,
			expectedStep: "5m",
		},
		{
			name:         "LastWeek",
			timeRange:    LastWeek,
			expectedDur:  7 * 24 * time.Hour,
			expectedStep: "1h",
		},
		{
			name:         "InvalidRange",
			timeRange:    TimeRange("invalid"),
			expectedDur:  30 * time.Minute,
			expectedStep: "30s",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			dur, step := GetTimeRangeConfig(tt.timeRange)
			assert.Equal(t, tt.expectedDur, dur)
			assert.Equal(t, tt.expectedStep, step)
		})
	}
}

func TestValidTimeRange(t *testing.T) {
	tests := []struct {
		name      string
		timeRange string
		expected  bool
	}{
		{
			name:      "Valid30m",
			timeRange: "30m",
			expected:  true,
		},
		{
			name:      "Valid1d",
			timeRange: "1d",
			expected:  true,
		},
		{
			name:      "Valid7d",
			timeRange: "7d",
			expected:  true,
		},
		{
			name:      "Invalid1h",
			timeRange: "1h",
			expected:  false,
		},
		{
			name:      "InvalidEmpty",
			timeRange: "",
			expected:  false,
		},
		{
			name:      "InvalidRandom",
			timeRange: "random",
			expected:  false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := ValidTimeRange(tt.timeRange)
			assert.Equal(t, tt.expected, result)
		})
	}
}

func TestTimeRangeConstants(t *testing.T) {
	assert.Equal(t, TimeRange("30m"), Last30Minutes)
	assert.Equal(t, TimeRange("1d"), LastDay)
	assert.Equal(t, TimeRange("7d"), LastWeek)
}

func TestDataPointStruct(t *testing.T) {
	now := time.Now()
	dp := DataPoint{
		Timestamp: now,
		Value:     42.5,
	}
	
	assert.Equal(t, now, dp.Timestamp)
	assert.Equal(t, 42.5, dp.Value)
}

func TestTimeSeriesStruct(t *testing.T) {
	now := time.Now()
	ts := TimeSeries{
		MetricName: "cpu_utilization",
		Labels:     map[string]string{"instance": "localhost"},
		Values: []DataPoint{
			{Timestamp: now, Value: 15.2},
			{Timestamp: now.Add(30 * time.Second), Value: 18.7},
		},
	}
	
	assert.Equal(t, "cpu_utilization", ts.MetricName)
	assert.Equal(t, "localhost", ts.Labels["instance"])
	assert.Len(t, ts.Values, 2)
	assert.Equal(t, 15.2, ts.Values[0].Value)
	assert.Equal(t, 18.7, ts.Values[1].Value)
}

func TestTimeSeriesResponse(t *testing.T) {
	startTime := time.Now().Add(-30 * time.Minute)
	endTime := time.Now()
	
	response := TimeSeriesResponse{
		TimeRange: "30m",
		StartTime: startTime,
		EndTime:   endTime,
		Step:      "30s",
		Series: []TimeSeries{
			{
				MetricName: "test_metric",
				Labels:     map[string]string{"job": "test"},
				Values:     []DataPoint{{Timestamp: startTime, Value: 100}},
			},
		},
	}
	
	assert.Equal(t, "30m", response.TimeRange)
	assert.Equal(t, startTime, response.StartTime)
	assert.Equal(t, endTime, response.EndTime)
	assert.Equal(t, "30s", response.Step)
	assert.Len(t, response.Series, 1)
	assert.Equal(t, "test_metric", response.Series[0].MetricName)
}

func TestSystemMetricsStruct(t *testing.T) {
	now := time.Now()
	metrics := SystemMetrics{
		Timestamp: now,
		CPU: CPUMetrics{
			Utilization: 25.5,
			ByCore:      map[string]float64{"0": 20.0, "1": 30.0},
		},
		Memory: MemoryMetrics{
			Total:       8_000_000_000,
			Used:        4_000_000_000,
			Free:        3_000_000_000,
			Cached:      1_000_000_000,
			Utilization: 50.0,
		},
		Disk: []DiskMetrics{
			{
				Device:      "/dev/sda1",
				Used:        100_000_000_000,
				Total:       500_000_000_000,
				Utilization: 20.0,
				IORate:      1_000_000,
			},
		},
		Network: []NetworkMetrics{
			{
				Interface: "eth0",
				RxRate:    1_000_000,
				TxRate:    500_000,
				Errors:    0,
			},
		},
	}
	
	assert.Equal(t, now, metrics.Timestamp)
	assert.Equal(t, 25.5, metrics.CPU.Utilization)
	assert.Equal(t, 20.0, metrics.CPU.ByCore["0"])
	assert.Equal(t, 8_000_000_000.0, metrics.Memory.Total)
	assert.Equal(t, 50.0, metrics.Memory.Utilization)
	assert.Len(t, metrics.Disk, 1)
	assert.Equal(t, "/dev/sda1", metrics.Disk[0].Device)
	assert.Len(t, metrics.Network, 1)
	assert.Equal(t, "eth0", metrics.Network[0].Interface)
}

func TestPortraitMetricsStruct(t *testing.T) {
	now := time.Now()
	metrics := PortraitMetrics{
		Timestamp: now,
		Requests: RequestMetrics{
			Total:           1000,
			Rate:            10.5,
			SuccessRate:     99.5,
			AverageDuration: 150_000,
		},
		Cache: CacheMetrics{
			HitRate:        85.5,
			OperationsRate: 50.0,
		},
		SceneComplexity: SceneMetrics{
			AverageSpheres: 25.0,
			AverageLights:  3.0,
		},
	}
	
	assert.Equal(t, now, metrics.Timestamp)
	assert.Equal(t, 1000.0, metrics.Requests.Total)
	assert.Equal(t, 10.5, metrics.Requests.Rate)
	assert.Equal(t, 99.5, metrics.Requests.SuccessRate)
	assert.Equal(t, 150_000.0, metrics.Requests.AverageDuration)
	assert.Equal(t, 85.5, metrics.Cache.HitRate)
	assert.Equal(t, 50.0, metrics.Cache.OperationsRate)
	assert.Equal(t, 25.0, metrics.SceneComplexity.AverageSpheres)
	assert.Equal(t, 3.0, metrics.SceneComplexity.AverageLights)
}