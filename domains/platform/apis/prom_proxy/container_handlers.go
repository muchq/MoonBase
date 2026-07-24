package prom_proxy

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
)

// imageTag pulls the tag off an image reference: ghcr.io/muchq/mithril:abc123
// -> abc123. Deploys pin images per commit, so that tag is the running
// revision. Returns "" for an untagged reference; the colon check is anchored
// past the last slash so a registry port (host:5000/img) isn't read as a tag.
func imageTag(image string) string {
	colon := strings.LastIndex(image, ":")
	if colon < 0 || colon < strings.LastIndex(image, "/") {
		return ""
	}
	return image[colon+1:]
}

// Compose names containers <project>-<service>-<index>, e.g. ubuntu-golf_hub-1.
// Strip both ends so a container can be addressed by the service it backs.
func containerDisplayName(name string) string {
	trimmed := strings.TrimPrefix(name, "ubuntu-")
	if i := strings.LastIndex(trimmed, "-"); i > 0 {
		if suffix := trimmed[i+1:]; suffix != "" && strings.Trim(suffix, "0123456789") == "" {
			trimmed = trimmed[:i]
		}
	}
	return trimmed
}

// matchesContainer accepts either the raw cAdvisor name or the service name, so
// callers don't have to know the compose naming convention.
func matchesContainer(containerName, requested string) bool {
	return containerName == requested || containerDisplayName(containerName) == requested
}

// GetContainers lists every container with its health. Unlike the service
// catalog this isn't registry-driven — infrastructure containers (caddy,
// postgres, prometheus) emit no app metrics at all, and they're exactly the
// ones with no other surface.
func (h *MetricsHandler) GetContainers(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	metrics, err := h.fetchContainerMetrics(ctx)
	if err != nil {
		mucks.JsonError(w, mucks.NewServerError(500))
		return
	}
	mucks.JsonOk(w, metrics)
}

// GetContainerDetail returns one container, addressed by container or service
// name. Unknown -> 404, matching the service endpoints.
func (h *MetricsHandler) GetContainerDetail(w http.ResponseWriter, r *http.Request) {
	requested := r.PathValue("name")

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	metrics, err := h.fetchContainerMetrics(ctx)
	if err != nil {
		mucks.JsonError(w, mucks.NewServerError(500))
		return
	}
	for i := range metrics.Containers {
		if matchesContainer(metrics.Containers[i].Name, requested) {
			mucks.JsonOk(w, metrics.Containers[i])
			return
		}
	}
	mucks.JsonError(w, mucks.NewNotFound())
}

// GetContainerTimeSeries returns one container's series over the range. The
// restarts series is the point: a crash loop that started and resolved
// overnight leaves no trace in a point-in-time view.
func (h *MetricsHandler) GetContainerTimeSeries(w http.ResponseWriter, r *http.Request) {
	requested := r.PathValue("name")
	rangeParam := r.PathValue("range")
	if !ValidTimeRange(rangeParam) {
		mucks.JsonError(w, mucks.NewBadRequest("Invalid time range. Valid options: 30m, 1d, 7d"))
		return
	}
	timeRange := TimeRange(rangeParam)

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	// Resolve to the real container name so the label filter matches whether
	// the caller addressed it by service or by container.
	metrics, err := h.fetchContainerMetrics(ctx)
	if err != nil {
		mucks.JsonError(w, mucks.NewServerError(500))
		return
	}
	name := ""
	for i := range metrics.Containers {
		if matchesContainer(metrics.Containers[i].Name, requested) {
			name = metrics.Containers[i].Name
			break
		}
	}
	if name == "" {
		mucks.JsonError(w, mucks.NewNotFound())
		return
	}

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

	queries := map[string]string{
		"cpu_usage":      fmt.Sprintf(`rate(container_cpu_usage_seconds_total{name="%s"}[5m])*100`, name),
		"memory_usage":   fmt.Sprintf(`container_memory_usage_bytes{name="%s"}`, name),
		"network_rx":     fmt.Sprintf(`rate(container_network_receive_bytes_total{name="%s"}[5m])`, name),
		"network_tx":     fmt.Sprintf(`rate(container_network_transmit_bytes_total{name="%s"}[5m])`, name),
		"restarts":       fmt.Sprintf(`changes(container_start_time_seconds{name="%s"}[5m])`, name),
		"uptime_seconds": fmt.Sprintf(`time()-container_start_time_seconds{name="%s"}`, name),
	}

	for metricName, query := range queries {
		resp, err := h.promClient.QueryRange(ctx, query, startTime, endTime, step)
		if err != nil {
			continue
		}
		for _, result := range resp.Data.Result {
			ts, err := extractTimeSeries(&result)
			if err != nil {
				continue
			}
			ts.MetricName = metricName
			response.Series = append(response.Series, ts)
		}
	}

	mucks.JsonOk(w, response)
}
