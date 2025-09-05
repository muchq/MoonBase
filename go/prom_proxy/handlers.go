package prom_proxy

import (
	"context"
	"fmt"
	"net/http"
	"strconv"
	"time"

	"github.com/muchq/moonbase/go/mucks"
)

// Helper function to extract float value from Prometheus result
func extractFloatValue(result *Result) (float64, error) {
	if len(result.Value) < 2 {
		return 0, fmt.Errorf("invalid prometheus result format")
	}
	
	valueStr, ok := result.Value[1].(string)
	if !ok {
		return 0, fmt.Errorf("value is not a string")
	}
	
	// Handle NaN, +Inf, -Inf values gracefully
	switch valueStr {
	case "NaN", "+Inf", "-Inf":
		return 0, nil // Return 0 for invalid mathematical results
	}
	
	return strconv.ParseFloat(valueStr, 64)
}

// Helper function to extract timeseries data from Prometheus range query result
func extractTimeSeries(result *Result) (TimeSeries, error) {
	ts := TimeSeries{
		Labels: result.Metric,
		Values: make([]DataPoint, 0, len(result.Values)),
	}
	
	// Set metric name from __name__ label or construct from labels
	if name, exists := result.Metric["__name__"]; exists {
		ts.MetricName = name
	} else {
		ts.MetricName = "unnamed_metric"
	}
	
	// Process each timestamp-value pair
	for _, valueArray := range result.Values {
		if len(valueArray) != 2 {
			continue
		}
		
		// Extract timestamp
		timestampFloat, ok := valueArray[0].(float64)
		if !ok {
			continue
		}
		timestamp := time.Unix(int64(timestampFloat), 0)
		
		// Extract value
		valueStr, ok := valueArray[1].(string)
		if !ok {
			continue
		}
		
		// Handle NaN, +Inf, -Inf values gracefully
		var value float64
		switch valueStr {
		case "NaN", "+Inf", "-Inf":
			value = 0 // Use 0 for invalid mathematical results
		default:
			var err error
			value, err = strconv.ParseFloat(valueStr, 64)
			if err != nil {
				continue
			}
		}
		
		ts.Values = append(ts.Values, DataPoint{
			Timestamp: timestamp,
			Value:     value,
		})
	}
	
	return ts, nil
}

// PrometheusQuerier interface for testing
type PrometheusQuerier interface {
	Query(ctx context.Context, query string) (*QueryResponse, error)
	QueryRange(ctx context.Context, query string, start, end time.Time, step string) (*QueryResponse, error)
}

type MetricsHandler struct {
	promClient PrometheusQuerier
	cache      *MetricsCache
}

func NewMetricsHandler(promClient PrometheusQuerier, cache *MetricsCache) *MetricsHandler {
	return &MetricsHandler{
		promClient: promClient,
		cache:      cache,
	}
}

func (h *MetricsHandler) HealthHandler(w http.ResponseWriter, r *http.Request) {
	response := map[string]string{
		"status": "healthy",
		"service": "prometheus-proxy",
		"timestamp": time.Now().UTC().Format(time.RFC3339),
	}
	mucks.JsonOk(w, response)
}

func (h *MetricsHandler) CacheStatusHandler(w http.ResponseWriter, r *http.Request) {
	cacheInfo := h.cache.GetCacheInfo()
	cacheInfo["status"] = "cache-info"
	cacheInfo["service"] = "prometheus-proxy-cache"
	cacheInfo["timestamp"] = time.Now().UTC().Format(time.RFC3339)
	
	mucks.JsonOk(w, cacheInfo)
}

func (h *MetricsHandler) GetSystemMetrics(w http.ResponseWriter, r *http.Request) {
	metrics := h.cache.GetSystemMetrics()
	if metrics == nil {
		problem := mucks.NewServerError(503)
		problem.Detail = "System metrics not available in cache"
		mucks.JsonError(w, problem)
		return
	}
	
	mucks.JsonOk(w, metrics)
}

func (h *MetricsHandler) GetPortraitMetrics(w http.ResponseWriter, r *http.Request) {
	metrics := h.cache.GetPortraitMetrics()
	if metrics == nil {
		problem := mucks.NewServerError(503)
		problem.Detail = "Portrait metrics not available in cache"
		mucks.JsonError(w, problem)
		return
	}
	
	mucks.JsonOk(w, metrics)
}

func (h *MetricsHandler) GetSummaryMetrics(w http.ResponseWriter, r *http.Request) {
	systemMetrics := h.cache.GetSystemMetrics()
	portraitMetrics := h.cache.GetPortraitMetrics()
	
	if systemMetrics == nil || portraitMetrics == nil {
		problem := mucks.NewServerError(503)
		problem.Detail = "Metrics not available in cache"
		mucks.JsonError(w, problem)
		return
	}
	
	summary := map[string]interface{}{
		"timestamp": time.Now().UTC(),
		"system": systemMetrics,
		"portrait": portraitMetrics,
	}
	
	mucks.JsonOk(w, summary)
}

func (h *MetricsHandler) GetSystemMetricsTimeSeries(w http.ResponseWriter, r *http.Request) {
	timeRange := r.PathValue("range")
	
	if !ValidTimeRange(timeRange) {
		problem := mucks.NewBadRequest("Invalid time range. Valid options: 30m, 1d, 7d")
		mucks.JsonError(w, problem)
		return
	}
	
	response := h.cache.GetSystemTimeseries(TimeRange(timeRange))
	if response == nil {
		problem := mucks.NewServerError(503)
		problem.Detail = "System timeseries not available in cache"
		mucks.JsonError(w, problem)
		return
	}
	
	mucks.JsonOk(w, response)
}

func (h *MetricsHandler) GetPortraitMetricsTimeSeries(w http.ResponseWriter, r *http.Request) {
	timeRange := r.PathValue("range")
	
	if !ValidTimeRange(timeRange) {
		problem := mucks.NewBadRequest("Invalid time range. Valid options: 30m, 1d, 7d")
		mucks.JsonError(w, problem)
		return
	}
	
	response := h.cache.GetPortraitTimeseries(TimeRange(timeRange))
	if response == nil {
		problem := mucks.NewServerError(503)
		problem.Detail = "Portrait timeseries not available in cache"
		mucks.JsonError(w, problem)
		return
	}
	
	mucks.JsonOk(w, response)
}

func (h *MetricsHandler) fetchSystemMetrics(ctx context.Context) (*SystemMetrics, error) {
	metrics := &SystemMetrics{
		Timestamp: time.Now().UTC(),
		CPU:       CPUMetrics{ByCore: make(map[string]float64)},
		Disk:      []DiskMetrics{},
		Network:   []NetworkMetrics{},
	}
	
	// Fetch CPU utilization
	cpuQuery := `100-avg(rate(system_cpu_time_seconds_total{state="idle"}[5m]))*100`
	cpuResp, err := h.promClient.Query(ctx, cpuQuery)
	if err == nil && len(cpuResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&cpuResp.Data.Result[0]); err == nil {
			metrics.CPU.Utilization = val
		}
	}
	
	// Fetch CPU by core
	cpuCoreQuery := `rate(system_cpu_time_seconds_total[5m])*100`
	cpuCoreResp, err := h.promClient.Query(ctx, cpuCoreQuery)
	if err == nil {
		for _, result := range cpuCoreResp.Data.Result {
			if core, exists := result.Metric["cpu"]; exists {
				if val, err := extractFloatValue(&result); err == nil {
					metrics.CPU.ByCore[core] = val
				}
			}
		}
	}
	
	// Fetch memory metrics
	memoryUsedQuery := `system_memory_usage_bytes{state="used"}`
	memUsedResp, err := h.promClient.Query(ctx, memoryUsedQuery)
	if err == nil && len(memUsedResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&memUsedResp.Data.Result[0]); err == nil {
			metrics.Memory.Used = val
		}
	}
	
	memoryFreeQuery := `system_memory_usage_bytes{state="free"}`
	memFreeResp, err := h.promClient.Query(ctx, memoryFreeQuery)
	if err == nil && len(memFreeResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&memFreeResp.Data.Result[0]); err == nil {
			metrics.Memory.Free = val
		}
	}
	
	memoryCachedQuery := `system_memory_usage_bytes{state="cached"}`
	memCachedResp, err := h.promClient.Query(ctx, memoryCachedQuery)
	if err == nil && len(memCachedResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&memCachedResp.Data.Result[0]); err == nil {
			metrics.Memory.Cached = val
		}
	}
	
	// Calculate total and utilization
	metrics.Memory.Total = metrics.Memory.Used + metrics.Memory.Free + metrics.Memory.Cached
	if metrics.Memory.Total > 0 {
		metrics.Memory.Utilization = (metrics.Memory.Used / metrics.Memory.Total) * 100
	}
	
	// Fetch disk metrics
	diskUsageQuery := `system_filesystem_usage_bytes`
	diskResp, err := h.promClient.Query(ctx, diskUsageQuery)
	if err == nil {
		deviceMap := make(map[string]*DiskMetrics)
		
		for _, result := range diskResp.Data.Result {
			if device, exists := result.Metric["device"]; exists {
				if _, exists := deviceMap[device]; !exists {
					deviceMap[device] = &DiskMetrics{Device: device}
				}
				
				if val, err := extractFloatValue(&result); err == nil {
					if state, exists := result.Metric["state"]; exists {
						switch state {
						case "used":
							deviceMap[device].Used = val
						case "free":
							deviceMap[device].Total += val
						}
					}
				}
			}
		}
		
		// Convert map to slice and calculate utilization
		for _, disk := range deviceMap {
			disk.Total += disk.Used
			if disk.Total > 0 {
				disk.Utilization = (disk.Used / disk.Total) * 100
			}
			metrics.Disk = append(metrics.Disk, *disk)
		}
	}
	
	// Fetch disk I/O rates
	diskIOQuery := `rate(system_disk_io_bytes_total[5m])`
	diskIOResp, err := h.promClient.Query(ctx, diskIOQuery)
	if err == nil {
		deviceIOMap := make(map[string]float64)
		
		for _, result := range diskIOResp.Data.Result {
			if device, exists := result.Metric["device"]; exists {
				if val, err := extractFloatValue(&result); err == nil {
					deviceIOMap[device] += val
				}
			}
		}
		
		// Update disk metrics with I/O rates
		for i := range metrics.Disk {
			if ioRate, exists := deviceIOMap[metrics.Disk[i].Device]; exists {
				metrics.Disk[i].IORate = ioRate
			}
		}
	}
	
	// Fetch network metrics
	networkIOQuery := `rate(system_network_io_bytes_total[5m])`
	netIOResp, err := h.promClient.Query(ctx, networkIOQuery)
	if err == nil {
		interfaceMap := make(map[string]*NetworkMetrics)
		
		for _, result := range netIOResp.Data.Result {
			if iface, exists := result.Metric["device"]; exists {
				if _, exists := interfaceMap[iface]; !exists {
					interfaceMap[iface] = &NetworkMetrics{Interface: iface}
				}
				
				if val, err := extractFloatValue(&result); err == nil {
					if direction, exists := result.Metric["direction"]; exists {
						switch direction {
						case "receive":
							interfaceMap[iface].RxRate = val
						case "transmit":
							interfaceMap[iface].TxRate = val
						}
					}
				}
			}
		}
		
		// Convert map to slice
		for _, netMetric := range interfaceMap {
			metrics.Network = append(metrics.Network, *netMetric)
		}
	}
	
	return metrics, nil
}

func (h *MetricsHandler) fetchSystemMetricsTimeSeries(ctx context.Context, timeRange TimeRange) (*TimeSeriesResponse, error) {
	duration, step := GetTimeRangeConfig(timeRange)
	endTime := time.Now().UTC()
	startTime := endTime.Add(-duration)
	
	response := &TimeSeriesResponse{
		TimeRange: string(timeRange),
		StartTime: startTime,
		EndTime:   endTime,
		Step:      step,
		Series:    []TimeSeries{},
	}
	
	// Define key system metrics queries
	queries := map[string]string{
		"cpu_utilization":    `100-avg(rate(system_cpu_time_seconds_total{state="idle"}[5m]))*100`,
		"memory_utilization": `system_memory_usage_bytes{state="used"}/on()group_left()(sum(system_memory_usage_bytes))*100`,
		"disk_io_rate":       `rate(system_disk_io_bytes_total[5m])`,
		"network_rx_rate":    `rate(system_network_io_bytes_total{direction="receive"}[5m])`,
		"network_tx_rate":    `rate(system_network_io_bytes_total{direction="transmit"}[5m])`,
	}
	
	// Execute each query as a range query
	for metricName, query := range queries {
		resp, err := h.promClient.QueryRange(ctx, query, startTime, endTime, step)
		if err != nil {
			// Log error but continue with other metrics
			continue
		}
		
		// Process results and add to response
		for _, result := range resp.Data.Result {
			ts, err := extractTimeSeries(&result)
			if err != nil {
				continue
			}
			ts.MetricName = metricName
			response.Series = append(response.Series, ts)
		}
	}
	
	return response, nil
}

func (h *MetricsHandler) fetchPortraitMetricsTimeSeries(ctx context.Context, timeRange TimeRange) (*TimeSeriesResponse, error) {
	duration, step := GetTimeRangeConfig(timeRange)
	endTime := time.Now().UTC()
	startTime := endTime.Add(-duration)
	
	response := &TimeSeriesResponse{
		TimeRange: string(timeRange),
		StartTime: startTime,
		EndTime:   endTime,
		Step:      step,
		Series:    []TimeSeries{},
	}
	
	// Define key portrait application metrics queries
	queries := map[string]string{
		"request_rate":           `rate(trace_requests_total[5m])`,
		"request_success_rate":   `rate(trace_requests_completed_total[5m])/rate(trace_requests_total[5m])*100`,
		"request_duration_avg":   `rate(trace_request_duration_microseconds_sum[5m])/rate(trace_request_duration_microseconds_count[5m])`,
		"cache_hit_rate":         `rate(trace_cache_hits_total[5m])/(rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m]))*100`,
		"cache_operations_rate":  `rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m])`,
		"scene_sphere_count":     `scene_sphere_count_gauge`,
		"scene_light_count":      `scene_light_count_gauge`,
	}
	
	// Execute each query as a range query
	for metricName, query := range queries {
		resp, err := h.promClient.QueryRange(ctx, query, startTime, endTime, step)
		if err != nil {
			// Log error but continue with other metrics
			continue
		}
		
		// Process results and add to response
		for _, result := range resp.Data.Result {
			ts, err := extractTimeSeries(&result)
			if err != nil {
				continue
			}
			ts.MetricName = metricName
			response.Series = append(response.Series, ts)
		}
	}
	
	return response, nil
}

func (h *MetricsHandler) fetchPortraitMetrics(ctx context.Context) (*PortraitMetrics, error) {
	metrics := &PortraitMetrics{
		Timestamp: time.Now().UTC(),
	}
	
	// Fetch request total
	requestTotalQuery := `trace_requests_total`
	reqTotalResp, err := h.promClient.Query(ctx, requestTotalQuery)
	if err == nil && len(reqTotalResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&reqTotalResp.Data.Result[0]); err == nil {
			metrics.Requests.Total = val
		}
	}
	
	// Fetch request rate
	requestRateQuery := `rate(trace_requests_total[5m])`
	reqRateResp, err := h.promClient.Query(ctx, requestRateQuery)
	if err == nil && len(reqRateResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&reqRateResp.Data.Result[0]); err == nil {
			metrics.Requests.Rate = val
		}
	}
	
	// Fetch request success rate
	successRateQuery := `rate(trace_requests_completed_total[5m])/rate(trace_requests_total[5m])*100`
	successResp, err := h.promClient.Query(ctx, successRateQuery)
	if err == nil && len(successResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&successResp.Data.Result[0]); err == nil {
			metrics.Requests.SuccessRate = val
		}
	}
	
	// Fetch average request duration
	durationQuery := `rate(trace_request_duration_microseconds_sum[5m])/rate(trace_request_duration_microseconds_count[5m])`
	durationResp, err := h.promClient.Query(ctx, durationQuery)
	if err == nil && len(durationResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&durationResp.Data.Result[0]); err == nil {
			metrics.Requests.AverageDuration = val
		}
	}
	
	// Fetch cache hit rate
	cacheHitRateQuery := `rate(trace_cache_hits_total[5m])/(rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m]))*100`
	cacheHitResp, err := h.promClient.Query(ctx, cacheHitRateQuery)
	if err == nil && len(cacheHitResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&cacheHitResp.Data.Result[0]); err == nil {
			metrics.Cache.HitRate = val
		}
	}
	
	// Fetch cache operations rate
	cacheOpsQuery := `rate(trace_cache_hits_total[5m])+rate(trace_cache_misses_total[5m])`
	cacheOpsResp, err := h.promClient.Query(ctx, cacheOpsQuery)
	if err == nil && len(cacheOpsResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&cacheOpsResp.Data.Result[0]); err == nil {
			metrics.Cache.OperationsRate = val
		}
	}
	
	// Fetch scene complexity metrics
	sphereCountQuery := `avg_over_time(scene_sphere_count_gauge[1h])`
	sphereResp, err := h.promClient.Query(ctx, sphereCountQuery)
	if err == nil && len(sphereResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&sphereResp.Data.Result[0]); err == nil {
			metrics.SceneComplexity.AverageSpheres = val
		}
	}
	
	lightCountQuery := `avg_over_time(scene_light_count_gauge[1h])`
	lightResp, err := h.promClient.Query(ctx, lightCountQuery)
	if err == nil && len(lightResp.Data.Result) > 0 {
		if val, err := extractFloatValue(&lightResp.Data.Result[0]); err == nil {
			metrics.SceneComplexity.AverageLights = val
		}
	}
	
	return metrics, nil
}
