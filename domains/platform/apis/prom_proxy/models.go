package prom_proxy

import "time"

// TimeRange represents preconfigured time ranges
type TimeRange string

const (
	Last30Minutes TimeRange = "30m"
	LastDay       TimeRange = "1d" 
	LastWeek      TimeRange = "7d"
)

// TimeSeries represents a single metric with timestamps
type TimeSeries struct {
	MetricName string      `json:"metric_name"`
	Labels     map[string]string `json:"labels,omitempty"`
	Values     []DataPoint `json:"values"`
}

// DataPoint represents a single timestamped value
type DataPoint struct {
	Timestamp time.Time `json:"timestamp"`
	Value     float64   `json:"value"`
}

// TimeSeriesResponse wraps timeseries data with metadata
type TimeSeriesResponse struct {
	TimeRange string       `json:"time_range"`
	StartTime time.Time    `json:"start_time"`
	EndTime   time.Time    `json:"end_time"`
	Step      string       `json:"step"`
	Series    []TimeSeries `json:"series"`
}

// GetTimeRangeConfig returns duration and step for a given time range
func GetTimeRangeConfig(timeRange TimeRange) (duration time.Duration, step string) {
	switch timeRange {
	case Last30Minutes:
		return 30 * time.Minute, "30s"
	case LastDay:
		return 24 * time.Hour, "5m"
	case LastWeek:
		return 7 * 24 * time.Hour, "1h"
	default:
		return 30 * time.Minute, "30s"
	}
}

// ValidTimeRange checks if a time range string is valid
func ValidTimeRange(tr string) bool {
	switch TimeRange(tr) {
	case Last30Minutes, LastDay, LastWeek:
		return true
	default:
		return false
	}
}

type SystemMetrics struct {
	Timestamp time.Time      `json:"timestamp"`
	CPU       CPUMetrics     `json:"cpu"`
	Memory    MemoryMetrics  `json:"memory"`
	Disk      []DiskMetrics  `json:"disk"`
	Network   []NetworkMetrics `json:"network"`
}

type CPUMetrics struct {
	Utilization float64            `json:"utilization_percent"`
	ByCore      map[string]float64 `json:"by_core"`
}

type MemoryMetrics struct {
	Total       float64 `json:"total_bytes"`
	Used        float64 `json:"used_bytes"`
	Free        float64 `json:"free_bytes"`
	Cached      float64 `json:"cached_bytes"`
	Utilization float64 `json:"utilization_percent"`
}

type DiskMetrics struct {
	Device      string  `json:"device"`
	Used        float64 `json:"used_bytes"`
	Total       float64 `json:"total_bytes"`
	Utilization float64 `json:"utilization_percent"`
	IORate      float64 `json:"io_rate_bytes_per_sec"`
}

type NetworkMetrics struct {
	Interface string  `json:"interface"`
	RxRate    float64 `json:"rx_rate_bytes_per_sec"`
	TxRate    float64 `json:"tx_rate_bytes_per_sec"`
	Errors    float64 `json:"errors_per_sec"`
}

type ContainerMetrics struct {
	Timestamp  time.Time             `json:"timestamp"`
	Containers []ContainerStats      `json:"containers"`
}

type ContainerStats struct {
	Name                string  `json:"name"`
	CPUUsagePercent     float64 `json:"cpu_usage_percent"`
	CPUThrottledSeconds float64 `json:"cpu_throttled_seconds"`
	MemoryUsageBytes    float64 `json:"memory_usage_bytes"`
	MemoryLimitBytes    float64 `json:"memory_limit_bytes"`
	MemoryUsagePercent  float64 `json:"memory_usage_percent"`
	NetworkRxBytes      float64 `json:"network_rx_bytes_per_sec"`
	NetworkTxBytes      float64 `json:"network_tx_bytes_per_sec"`
}

type ServiceCatalogEntry struct {
	Name      string `json:"name"`
	HasCustom bool   `json:"has_custom"`
}

type ServiceCatalog struct {
	Services []ServiceCatalogEntry `json:"services"`
}

type HostMetricsResponse struct {
	Timestamp  time.Time        `json:"timestamp"`
	System     *SystemMetrics   `json:"system"`
	Containers []ContainerStats `json:"containers"`
}

type StandardMetrics struct {
	RequestsTotal     float64 `json:"requests_total"`
	RatePerSec        float64 `json:"rate_per_sec"`
	SuccessCount5m    float64 `json:"success_count_5m"`
	FailureCount5m    float64 `json:"failure_count_5m"`
	ErrorRatePercent  float64 `json:"error_rate_percent"`
	AvgDurationMicros float64 `json:"avg_duration_microseconds"`
	P95DurationMicros float64 `json:"p95_duration_microseconds"`
	ActiveRequests    float64 `json:"active_requests"`
}

type CustomMetricValue struct {
	Label string  `json:"label"`
	Value float64 `json:"value"`
	Unit  string  `json:"unit"`
}

type CustomMetricGroup struct {
	Title   string              `json:"title"`
	Metrics []CustomMetricValue `json:"metrics"`
}

type ServiceMetricsResponse struct {
	Timestamp time.Time           `json:"timestamp"`
	Service   string              `json:"service"`
	Standard  StandardMetrics     `json:"standard"`
	Custom    []CustomMetricGroup `json:"custom"`
}
