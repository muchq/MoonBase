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
	// A digest reference (img@sha256:...) has no tag. Returning the hex would
	// be worse than returning nothing: 64 hex chars in a "running version"
	// field is indistinguishable from a commit SHA, so a wrong answer would
	// look like a right one.
	if strings.Contains(image[strings.LastIndex(image, "/")+1:], "@") {
		return ""
	}
	colon := strings.LastIndex(image, ":")
	if colon < 0 || colon < strings.LastIndex(image, "/") {
		return ""
	}
	return image[colon+1:]
}

// Compose names containers <project>-<service>-<index>, e.g. ubuntu-games_hub-1.
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

// resolveContainer finds the ref a request addresses, by container name or by
// service. An exact name always wins: otherwise a request for a container that
// exists could be answered with a different one whose service happens to match.
// refs are sorted, so a tie between replicas resolves the same way every time.
func resolveContainer(refs []containerRef, requested string) (containerRef, bool) {
	for _, ref := range refs {
		if ref.name == requested {
			return ref, true
		}
	}
	for _, ref := range refs {
		if ref.service == requested {
			return ref, true
		}
	}
	return containerRef{}, false
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
		// Same resilience contract as the rest of the API: a failing scrape
		// source yields an empty section with 200, never a 500.
		metrics = &ContainerMetrics{Timestamp: time.Now().UTC(), Containers: []ContainerStats{}}
	}
	mucks.JsonOk(w, metrics)
}

// GetContainerDetail returns one container, addressed by container or service
// name. Unknown -> 404, matching the service endpoints.
func (h *MetricsHandler) GetContainerDetail(w http.ResponseWriter, r *http.Request) {
	requested := r.PathValue("name")

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	refs, err := h.listContainers(ctx)
	if err != nil {
		// Unlike the listing, a single-resource lookup can't degrade to an
		// empty answer: with the listing unavailable there's no way to tell a
		// missing container from an unknown one.
		problem := mucks.NewServerError(500)
		problem.Detail = "Failed to list containers: " + err.Error()
		mucks.JsonError(w, problem)
		return
	}
	ref, found := resolveContainer(refs, requested)
	if !found {
		mucks.JsonError(w, mucks.NewNotFound())
		return
	}

	stats := h.containerStats(ctx, ref)
	mucks.JsonOk(w, ContainerDetail{Timestamp: time.Now().UTC(), Container: stats})
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
	// the caller addressed it by service or by container. The listing alone is
	// enough — fetching every container's stats here would discard all of them.
	refs, err := h.listContainers(ctx)
	if err != nil {
		problem := mucks.NewServerError(500)
		problem.Detail = "Failed to list containers: " + err.Error()
		mucks.JsonError(w, problem)
		return
	}
	ref, found := resolveContainer(refs, requested)
	if !found {
		mucks.JsonError(w, mucks.NewNotFound())
		return
	}
	name := ref.name

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
		"cpu_usage":    fmt.Sprintf(`rate(container_cpu_usage_seconds_total{name="%s"}[5m])*100`, name),
		"memory_usage": fmt.Sprintf(`container_memory_usage_bytes{name="%s"}`, name),
		// sum() so a multi-interface container yields one series per metric
		// rather than several sharing a metric_name.
		"network_rx": fmt.Sprintf(`sum(rate(container_network_receive_bytes_total{name="%s"}[5m]))`, name),
		"network_tx": fmt.Sprintf(`sum(rate(container_network_transmit_bytes_total{name="%s"}[5m]))`, name),
		// Window tracks the step: at 7d the step is 1h, so a [5m] window would
		// only ever inspect the last 5 minutes of each hour and drop most
		// restarts — precisely the overnight crash loop this exists to show.
		"restarts":       fmt.Sprintf(`changes(container_start_time_seconds{name="%s"}[%s])`, name, step),
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
