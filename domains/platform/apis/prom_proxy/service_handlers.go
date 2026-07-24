package prom_proxy

import (
	"context"
	"net/http"
	"strings"
	"time"

	"github.com/muchq/moonbase/domains/platform/libs/mucks"
)

// Handlers for the dashboard overhaul (#1199): the service catalog, the
// merged host page, and the generic per-service pages driven by the
// registry. The legacy per-service routes stay up until the UI cutover.

func (h *MetricsHandler) GetServiceCatalog(w http.ResponseWriter, r *http.Request) {
	catalog := ServiceCatalog{Services: []ServiceCatalogEntry{}}
	for _, name := range serviceOrder {
		entry := serviceRegistry[name]
		catalog.Services = append(catalog.Services, ServiceCatalogEntry{
			Name:      name,
			HasCustom: len(entry.CustomScalars) > 0,
		})
	}
	mucks.JsonOk(w, catalog)
}

func (h *MetricsHandler) GetHostMetrics(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	response := &HostMetricsResponse{
		Timestamp:  time.Now().UTC(),
		Containers: []ContainerStats{},
	}
	// Same resilience contract as the rest of the API: a failing scrape
	// source yields zeroed/empty sections with 200, never a 500.
	if system, err := h.fetchSystemMetrics(ctx); err == nil {
		response.System = system
	}
	if containers, err := h.fetchContainerMetrics(ctx); err == nil {
		response.Containers = containers.Containers
	}

	mucks.JsonOk(w, response)
}

func (h *MetricsHandler) GetHostMetricsTimeSeries(w http.ResponseWriter, r *http.Request) {
	timeRange := r.PathValue("range")

	if !ValidTimeRange(timeRange) {
		problem := mucks.NewBadRequest("Invalid time range. Valid options: 30m, 1d, 7d")
		mucks.JsonError(w, problem)
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	response, err := h.fetchSystemMetricsTimeSeries(ctx, TimeRange(timeRange))
	if err != nil {
		problem := mucks.NewServerError(500)
		problem.Detail = "Failed to fetch host metrics timeseries: " + err.Error()
		mucks.JsonError(w, problem)
		return
	}

	// Container series ride the same response, namespaced so they can't
	// collide with the host series names.
	if containers, err := h.fetchContainerMetricsTimeSeries(ctx, TimeRange(timeRange)); err == nil {
		for _, ts := range containers.Series {
			if !strings.HasPrefix(ts.MetricName, "container_") {
				ts.MetricName = "container_" + ts.MetricName
			}
			response.Series = append(response.Series, ts)
		}
	}

	mucks.JsonOk(w, response)
}

func (h *MetricsHandler) GetServiceMetrics(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	entry, known := serviceRegistry[name]
	if !known {
		mucks.JsonError(w, mucks.NewNotFound())
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	response := &ServiceMetricsResponse{
		Timestamp: time.Now().UTC(),
		Service:   name,
		Custom:    []CustomMetricGroup{},
	}

	for _, q := range standardScalarQueries(name) {
		resp, err := h.promClient.Query(ctx, q.Query)
		if err == nil && len(resp.Data.Result) > 0 {
			if val, err := extractFloatValue(&resp.Data.Result[0]); err == nil {
				*q.Field(&response.Standard) = val
			}
		}
	}

	// Groups keep registry order; a failed query leaves its descriptor in
	// place with a zero value, so the page shape is stable under outages.
	groupIndex := map[string]int{}
	for _, def := range entry.CustomScalars {
		value := 0.0
		resp, err := h.promClient.Query(ctx, def.Query)
		if err == nil && len(resp.Data.Result) > 0 {
			if val, err := extractFloatValue(&resp.Data.Result[0]); err == nil {
				value = val
			}
		}
		i, seen := groupIndex[def.Group]
		if !seen {
			i = len(response.Custom)
			groupIndex[def.Group] = i
			response.Custom = append(response.Custom, CustomMetricGroup{Title: def.Group})
		}
		response.Custom[i].Metrics = append(response.Custom[i].Metrics, CustomMetricValue{
			Label: def.Label,
			Value: value,
			Unit:  def.Unit,
		})
	}

	mucks.JsonOk(w, response)
}

func (h *MetricsHandler) GetServiceMetricsTimeSeries(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	entry, known := serviceRegistry[name]
	if !known {
		mucks.JsonError(w, mucks.NewNotFound())
		return
	}

	timeRange := r.PathValue("range")
	if !ValidTimeRange(timeRange) {
		problem := mucks.NewBadRequest("Invalid time range. Valid options: 30m, 1d, 7d")
		mucks.JsonError(w, problem)
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	duration, step := GetTimeRangeConfig(TimeRange(timeRange))
	endTime := time.Now().UTC()
	startTime := endTime.Add(-duration)

	response := &TimeSeriesResponse{
		TimeRange: timeRange,
		StartTime: startTime,
		EndTime:   endTime,
		Step:      step,
		Series:    []TimeSeries{},
	}

	queries := standardTimeseriesQueries(name)
	for metricName, query := range entry.CustomTimeseries {
		queries[metricName] = query
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
