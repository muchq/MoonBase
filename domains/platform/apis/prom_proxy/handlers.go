package prom_proxy

import (
	"context"
	"fmt"
	"net/http"
	"sort"
	"strconv"
	"time"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
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
}

func NewMetricsHandler(promClient PrometheusQuerier) *MetricsHandler {
	return &MetricsHandler{
		promClient: promClient,
	}
}

func (h *MetricsHandler) HealthHandler(w http.ResponseWriter, r *http.Request) {
	response := map[string]string{
		"status":    "healthy",
		"service":   "prometheus-proxy",
		"timestamp": time.Now().UTC().Format(time.RFC3339),
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

// containerRef is one row of the container listing: the cAdvisor name, the
// compose service behind it, and the image it is running.
type containerRef struct {
	name    string
	service string
	image   string
}

// listContainers is a single query, so callers that only need to resolve a
// name don't pay for the whole per-container fan-out.
//
// max() rather than count() so the value is the last-seen timestamp. Grouping
// by image means one container can yield two rows: for a few minutes after a
// redeploy the replaced container is still inside Prometheus's lookback
// window alongside its replacement. Keeping the newer sample per name is what
// stops the dashboard reporting the previous revision as the running one,
// right when someone is watching a deploy.
func (h *MetricsHandler) listContainers(ctx context.Context) ([]containerRef, error) {
	const query = `max by (name, image, container_label_com_docker_compose_service) (container_last_seen)`
	resp, err := h.promClient.Query(ctx, query)
	if err != nil {
		return nil, err
	}

	newest := map[string]float64{}
	byName := map[string]containerRef{}
	for i := range resp.Data.Result {
		result := &resp.Data.Result[i]
		name := result.Metric["name"]
		if name == "" {
			continue
		}
		lastSeen, err := extractFloatValue(result)
		if err != nil {
			lastSeen = 0
		}
		if prev, seen := newest[name]; seen && lastSeen <= prev {
			continue
		}
		newest[name] = lastSeen

		// Compose stamps the service on the container; the label is exact
		// where parsing the name is a guess about the project prefix, which
		// differs between the deployed host and local_deploy.sh.
		service := result.Metric["container_label_com_docker_compose_service"]
		if service == "" {
			service = containerDisplayName(name)
		}
		byName[name] = containerRef{name: name, service: service, image: result.Metric["image"]}
	}

	// Sorted so first-match resolution is deterministic rather than dependent
	// on Prometheus's vector order.
	names := make([]string, 0, len(byName))
	for name := range byName {
		names = append(names, name)
	}
	sort.Strings(names)

	refs := make([]containerRef, 0, len(names))
	for _, name := range names {
		refs = append(refs, byName[name])
	}
	return refs, nil
}

// containerScalars holds the ten per-container metrics, each indexed by
// container name. Absence from a map means the query failed or cAdvisor has no
// sample — never zero.
type containerScalars struct {
	cpu, throttled, memUsage, memLimit, netRx, netTx map[string]float64
	restarts, uptime, lastSeen, oom                  map[string]float64
}

// fetchContainerScalars answers every per-container metric for the containers
// the selector matches — `name!=""` for the whole host, `name="x"` for one.
// One query per metric either way: the listing used to issue ten queries per
// container, which at host scale was ~181 per dashboard poll and held
// Prometheus at ~6% CPU for as long as a tab stayed open.
func (h *MetricsHandler) fetchContainerScalars(ctx context.Context, selector string) containerScalars {
	q := func(template string) map[string]float64 {
		return h.queryVector(ctx, fmt.Sprintf(template, selector))
	}
	return containerScalars{
		cpu:       q(`sum by (name) (rate(container_cpu_usage_seconds_total{%s}[5m]))*100`),
		throttled: q(`sum by (name) (rate(container_cpu_cfs_throttled_seconds_total{%s}[5m]))`),
		memUsage:  q(`max by (name) (container_memory_usage_bytes{%s})`),
		memLimit:  q(`max by (name) (container_spec_memory_limit_bytes{%s})`),
		netRx:     q(`sum by (name) (rate(container_network_receive_bytes_total{%s}[5m]))`),
		netTx:     q(`sum by (name) (rate(container_network_transmit_bytes_total{%s}[5m]))`),
		// cAdvisor exposes no restart counter, so count the steps in the
		// container's start time: each restart re-stamps it.
		restarts: q(`max by (name) (changes(container_start_time_seconds{%s}[1h]))`),
		uptime:   q(`time()-max by (name) (container_start_time_seconds{%s})`),
		// How stale cAdvisor's view of a container is. Unlike uptime, this
		// keeps answering after a container stops — the series lingers until
		// retention drops it — which is what lets a dead container stay on
		// the page instead of vanishing from it.
		lastSeen: q(`time()-max by (name) (container_last_seen{%s})`),
		oom:      q(`sum by (name) (increase(container_oom_events_total{%s}[1h]))`),
	}
}

// queryVector runs one instant query and indexes the result by container name.
// An error degrades to an empty map, which downstream reads as "no data" —
// the same no-verdict contract a failed per-container query had.
func (h *MetricsHandler) queryVector(ctx context.Context, query string) map[string]float64 {
	resp, err := h.promClient.Query(ctx, query)
	if err != nil {
		return nil
	}
	out := make(map[string]float64, len(resp.Data.Result))
	for i := range resp.Data.Result {
		result := &resp.Data.Result[i]
		name := result.Metric["name"]
		if name == "" {
			continue
		}
		val, err := extractFloatValue(result)
		if err != nil {
			continue
		}
		out[name] = val
	}
	return out
}

func (s containerScalars) stats(ref containerRef) ContainerStats {
	stats := ContainerStats{
		Name:    ref.name,
		Service: ref.service,
		Image:   ref.image,
		Version: imageTag(ref.image),
	}
	get := func(m map[string]float64) (float64, bool) {
		val, ok := m[ref.name]
		return val, ok
	}

	if val, ok := get(s.cpu); ok {
		stats.CPUUsagePercent = val
	}
	if val, ok := get(s.throttled); ok {
		stats.CPUThrottledSeconds = val
	}
	if val, ok := get(s.memUsage); ok {
		stats.MemoryUsageBytes = val
	}
	if val, ok := get(s.memLimit); ok {
		stats.MemoryLimitBytes = val
		if stats.MemoryUsageBytes > 0 && val > 0 {
			stats.MemoryUsagePercent = (stats.MemoryUsageBytes / val) * 100
		}
	}
	if val, ok := get(s.netRx); ok {
		stats.NetworkRxBytes = val
	}
	if val, ok := get(s.netTx); ok {
		stats.NetworkTxBytes = val
	}
	if val, ok := get(s.restarts); ok {
		stats.RestartsLastHour = val
	}
	uptime, reporting := get(s.uptime)
	stats.UptimeSeconds = uptime
	if val, ok := get(s.lastSeen); ok {
		stats.LastSeenAgoSeconds = val
	}
	// Only where cAdvisor ships the counter; a build without it leaves zero,
	// which reads the same as no kills. Not part of the reporting signal for
	// that reason.
	if val, ok := get(s.oom); ok {
		stats.OOMEventsLastHour = val
	}

	// Uptime is the liveness signal: a running container always has one. Its
	// absence means the query failed or cAdvisor has nothing, and claiming
	// crash_looping=false there would report a container we can't see as
	// healthy — the exact false negative these fields exist to prevent.
	stats.Reporting = reporting
	stats.CrashLooping = reporting && isCrashLooping(stats.RestartsLastHour, stats.UptimeSeconds)
	return stats
}

// containerStats runs the per-container queries for one container, so a
// single-container request doesn't fan out across the whole stack.
func (h *MetricsHandler) containerStats(ctx context.Context, ref containerRef) ContainerStats {
	return h.fetchContainerScalars(ctx, fmt.Sprintf(`name=%q`, ref.name)).stats(ref)
}

func (h *MetricsHandler) fetchContainerMetrics(ctx context.Context) (*ContainerMetrics, error) {
	refs, err := h.listContainers(ctx)
	if err != nil {
		return nil, err
	}

	scalars := h.fetchContainerScalars(ctx, `name!=""`)
	metrics := &ContainerMetrics{
		Timestamp:  time.Now().UTC(),
		Containers: []ContainerStats{},
	}
	for _, ref := range refs {
		metrics.Containers = append(metrics.Containers, scalars.stats(ref))
	}
	return metrics, nil
}

// A container is crash-looping when it keeps restarting and never stays up:
// repeated restarts alone could be one bad hour it recovered from, and a young
// container alone is just a fresh deploy. Together they mean it can't start.
const (
	crashLoopMinRestarts = 3
	crashLoopMaxUptime   = 300 // seconds
)

func isCrashLooping(restartsLastHour, uptimeSeconds float64) bool {
	return restartsLastHour >= crashLoopMinRestarts && uptimeSeconds < crashLoopMaxUptime
}

func (h *MetricsHandler) fetchContainerMetricsTimeSeries(ctx context.Context, timeRange TimeRange) (*TimeSeriesResponse, error) {
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

	// Define key container metrics queries
	queries := map[string]string{
		"cpu_usage":            `rate(container_cpu_usage_seconds_total[5m])*100`,
		"cpu_throttled":        `rate(container_cpu_cfs_throttled_seconds_total[5m])`,
		"memory_usage":         `container_memory_usage_bytes`,
		"memory_usage_percent": `(container_memory_usage_bytes/container_spec_memory_limit_bytes)*100`,
		"network_rx":           `rate(container_network_receive_bytes_total[5m])`,
		"network_tx":           `rate(container_network_transmit_bytes_total[5m])`,
		// The history a point-in-time check can't give: restarts over the
		// window, so a crash loop that resolved overnight is still visible.
		"restarts": fmt.Sprintf(`changes(container_start_time_seconds[%s])`, step),
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
