package prom_proxy

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// ---------------------------------------------------------------- unit

func TestImageTag(t *testing.T) {
	tests := []struct {
		name  string
		image string
		want  string
	}{
		{"pinned image", "ghcr.io/muchq/mithril:" + strings.Repeat("a", 40), strings.Repeat("a", 40)},
		{"latest", "ghcr.io/muchq/mithril:latest", "latest"},
		{"third party with version", "postgres:18", "18"},
		{"tag with dots", "otel/opentelemetry-collector-contrib:0.155.0", "0.155.0"},
		// Negatives: report nothing rather than something wrong.
		{"untagged", "ghcr.io/muchq/mithril", ""},
		{"empty", "", ""},
		{"trailing colon", "img:", ""},
		// A registry port is a colon that is not a tag; reading it as one
		// would report "5000" as the running revision.
		{"registry port, no tag", "registry.local:5000/mithril", ""},
		{"registry port with tag", "registry.local:5000/mithril:abc123", "abc123"},
		// A digest is not a tag. 64 hex chars in a "version" field would be
		// indistinguishable from a commit SHA — a wrong answer that looks right.
		{"digest", "ghcr.io/muchq/mithril@sha256:" + strings.Repeat("e", 64), ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.want, imageTag(tt.image))
		})
	}
}

func TestContainerDisplayName(t *testing.T) {
	tests := []struct {
		name string
		in   string
		want string
	}{
		{"compose naming", "ubuntu-golf_hub-1", "golf_hub"},
		{"hyphenated service", "ubuntu-microgpt-serve-1", "microgpt-serve"},
		{"underscored service", "ubuntu-shared_postgres-1", "shared_postgres"},
		{"infrastructure container", "ubuntu-cadvisor-1", "cadvisor"},
		// A bare "-1" strip would turn this into "svc0".
		{"double digit index", "ubuntu-svc-10", "svc"},
		{"no project prefix", "mithril-1", "mithril"},
		{"no index", "cadvisor", "cadvisor"},
		{"non numeric suffix", "ubuntu-one_d4-postgres", "one_d4-postgres"},
		{"empty", "", ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.Equal(t, tt.want, containerDisplayName(tt.in))
		})
	}
}

func TestResolveContainer(t *testing.T) {
	refs := []containerRef{
		{name: "ubuntu-mithril-1", service: "mithril"},
		{name: "ubuntu-one_d4-1", service: "one_d4"},
		{name: "mithril", service: "something_else"},
	}

	got, found := resolveContainer(refs, "one_d4")
	require.True(t, found)
	assert.Equal(t, "ubuntu-one_d4-1", got.name, "by service")

	// An exact container name must win over another container whose service
	// happens to match, or a request resolves to the wrong container.
	got, found = resolveContainer(refs, "mithril")
	require.True(t, found)
	assert.Equal(t, "mithril", got.name, "exact name beats service match")

	_, found = resolveContainer(refs, "nope")
	assert.False(t, found)
	// Substring must not match, or "d4" would resolve to one_d4.
	_, found = resolveContainer(refs, "d4")
	assert.False(t, found)
}

// ---------------------------------------------------------------- fixtures

const listQuery = `max by (name, image, container_label_com_docker_compose_service) (container_last_seen)`

// listResult builds one row of the container listing. lastSeen is the sample
// timestamp, which is how duplicate rows for one name are ordered.
func listResult(name, service, image string, lastSeen float64) Result {
	return Result{
		Metric: map[string]string{
			"name":  name,
			"image": image,
			"container_label_com_docker_compose_service": service,
		},
		Value: []interface{}{1609459200.0, fmt.Sprintf("%g", lastSeen)},
	}
}

func vectorResponse(results ...Result) *QueryResponse {
	return &QueryResponse{
		Status: "success",
		Data: struct {
			ResultType string   `json:"resultType"`
			Result     []Result `json:"result"`
		}{ResultType: "vector", Result: results},
	}
}

// A host whose posterize is crash-looping on the current revision while
// golf_hub is healthy — the shape of the incident these endpoints exist for.
// caddy is listed but has no per-container samples, standing in for a
// container cAdvisor isn't reporting on.
func containerFixture() *mockPrometheusClient {
	return &mockPrometheusClient{queryResponses: map[string]*QueryResponse{
		listQuery: vectorResponse(
			listResult("ubuntu-posterize-1", "posterize", "ghcr.io/muchq/posterize:abc123", 100),
			listResult("ubuntu-golf_hub-1", "golf_hub", "ghcr.io/muchq/golf_hub:abc123", 100),
			listResult("ubuntu-caddy-1", "caddy", "caddy:2-alpine", 100),
		),
		groupedRestartsQuery: vectorResponse(
			vectorResult("ubuntu-posterize-1", "47"),
			vectorResult("ubuntu-golf_hub-1", "0"),
		),
		groupedUptimeQuery: vectorResponse(
			vectorResult("ubuntu-posterize-1", "8"),
			vectorResult("ubuntu-golf_hub-1", "86400"),
		),
		// The same answers scoped to one container, for the detail endpoint.
		`max by (name) (changes(container_start_time_seconds{name="ubuntu-posterize-1"}[1h]))`: vectorResponse(vectorResult("ubuntu-posterize-1", "47")),
		`time()-max by (name) (container_start_time_seconds{name="ubuntu-posterize-1"})`:       vectorResponse(vectorResult("ubuntu-posterize-1", "8")),
	}}
}

func vectorResult(name, value string) Result {
	return Result{
		Metric: map[string]string{"name": name},
		Value:  []interface{}{1609459200.0, value},
	}
}

// The grouped per-metric queries the listing issues — one query per metric,
// covering every container at once.
const (
	groupedCPUQuery       = `sum by (name) (rate(container_cpu_usage_seconds_total{name!=""}[5m]))*100`
	groupedThrottledQuery = `sum by (name) (rate(container_cpu_cfs_throttled_seconds_total{name!=""}[5m]))`
	groupedMemUsageQuery  = `max by (name) (container_memory_usage_bytes{name!=""})`
	groupedMemLimitQuery  = `max by (name) (container_spec_memory_limit_bytes{name!=""})`
	groupedNetRxQuery     = `sum by (name) (rate(container_network_receive_bytes_total{name!=""}[5m]))`
	groupedNetTxQuery     = `sum by (name) (rate(container_network_transmit_bytes_total{name!=""}[5m]))`
	groupedRestartsQuery  = `max by (name) (changes(container_start_time_seconds{name!=""}[1h]))`
	groupedUptimeQuery    = `time()-max by (name) (container_start_time_seconds{name!=""})`
	groupedLastSeenQuery  = `time()-max by (name) (container_last_seen{name!=""})`
	groupedOOMQuery       = `sum by (name) (increase(container_oom_events_total{name!=""}[1h]))`
)

// ---------------------------------------------------------------- listing

// The listing's query count must not scale with the container count: ten
// queries per container is ~181 instant queries per dashboard poll at host
// scale, enough to hold Prometheus at ~6% CPU from one open tab.
func TestGetContainers_OneQueryPerMetricNotPerContainer(t *testing.T) {
	mock := containerFixture()
	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	assert.ElementsMatch(t, []string{
		listQuery,
		groupedCPUQuery,
		groupedThrottledQuery,
		groupedMemUsageQuery,
		groupedMemLimitQuery,
		groupedNetRxQuery,
		groupedNetTxQuery,
		groupedRestartsQuery,
		groupedUptimeQuery,
		groupedLastSeenQuery,
		groupedOOMQuery,
	}, mock.instantQueries, "one listing query plus one per metric, regardless of container count")
}

// Content coverage for every grouped template: each metric seeded with a
// distinct value must land in its ContainerStats field, and no query may miss
// the fixture — a template typo reads as no-data, never as a borrowed value.
func TestGetContainers_AllMetricsFlowThrough(t *testing.T) {
	name := "ubuntu-mithril-1"
	mock := &mockPrometheusClient{queryResponses: map[string]*QueryResponse{
		listQuery:             vectorResponse(listResult(name, "mithril", "ghcr.io/muchq/mithril:abc123", 100)),
		groupedCPUQuery:       vectorResponse(vectorResult(name, "12.5")),
		groupedThrottledQuery: vectorResponse(vectorResult(name, "0.25")),
		groupedMemUsageQuery:  vectorResponse(vectorResult(name, "256")),
		groupedMemLimitQuery:  vectorResponse(vectorResult(name, "1024")),
		groupedNetRxQuery:     vectorResponse(vectorResult(name, "1000")),
		groupedNetTxQuery:     vectorResponse(vectorResult(name, "2000")),
		groupedRestartsQuery:  vectorResponse(vectorResult(name, "1")),
		groupedUptimeQuery:    vectorResponse(vectorResult(name, "3600")),
		groupedLastSeenQuery:  vectorResponse(vectorResult(name, "15")),
		groupedOOMQuery:       vectorResponse(vectorResult(name, "3")),
	}}
	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	assert.Empty(t, mock.misses, "every query the listing issues has a fixture entry")
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	c := response.Containers[0]
	assert.Equal(t, 12.5, c.CPUUsagePercent)
	assert.Equal(t, 0.25, c.CPUThrottledSeconds)
	assert.Equal(t, 256.0, c.MemoryUsageBytes)
	assert.Equal(t, 1024.0, c.MemoryLimitBytes)
	assert.Equal(t, 25.0, c.MemoryUsagePercent, "derived from usage/limit")
	assert.Equal(t, 1000.0, c.NetworkRxBytes)
	assert.Equal(t, 2000.0, c.NetworkTxBytes)
	assert.Equal(t, 1.0, c.RestartsLastHour)
	assert.Equal(t, 3600.0, c.UptimeSeconds)
	assert.Equal(t, 15.0, c.LastSeenAgoSeconds)
	assert.Equal(t, 3.0, c.OOMEventsLastHour)
	assert.True(t, c.Reporting)
	assert.False(t, c.CrashLooping)
}

func TestGetContainers(t *testing.T) {
	handler := NewMetricsHandler(containerFixture())
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 3)

	byName := map[string]ContainerStats{}
	for _, c := range response.Containers {
		byName[c.Name] = c
	}

	posterize := byName["ubuntu-posterize-1"]
	assert.True(t, posterize.CrashLooping)
	assert.Equal(t, 47.0, posterize.RestartsLastHour)
	assert.Equal(t, 8.0, posterize.UptimeSeconds)
	assert.True(t, posterize.Reporting)
	assert.Equal(t, "ghcr.io/muchq/posterize:abc123", posterize.Image)
	assert.Equal(t, "abc123", posterize.Version, "running revision from the image tag")
	assert.Equal(t, "posterize", posterize.Service, "clients link to the service page with this")

	golfHub := byName["ubuntu-golf_hub-1"]
	assert.False(t, golfHub.CrashLooping, "healthy peer unaffected")
	assert.Equal(t, 86400.0, golfHub.UptimeSeconds)

	// Infrastructure containers emit no app metrics, so this listing is the
	// only place they appear at all.
	caddy := byName["ubuntu-caddy-1"]
	assert.Equal(t, "2-alpine", caddy.Version)
	// No samples for it: it must read as "not reporting", never as healthy.
	assert.False(t, caddy.Reporting)
	assert.False(t, caddy.CrashLooping)
	assert.Zero(t, caddy.UptimeSeconds)
}

// Grouping the listing by image lets one container yield two rows while a
// replaced container is still inside Prometheus's lookback window. Without
// dedup the dashboard shows it twice, and can report the old revision as the
// running one — during a deploy, which is exactly when someone is watching.
func TestGetContainers_DeduplicatesByName(t *testing.T) {
	mock := containerFixture()
	// Newest first, so "keep the last row" would pick the stale one — the
	// order that makes this assertion discriminating.
	mock.queryResponses[listQuery] = vectorResponse(
		listResult("ubuntu-mithril-1", "mithril", "ghcr.io/muchq/mithril:NEWSHA", 200),
		listResult("ubuntu-mithril-1", "mithril", "ghcr.io/muchq/mithril:OLDSHA", 100),
	)
	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1, "one container, not one per image")
	assert.Equal(t, "NEWSHA", response.Containers[0].Version, "the newer sample is what's running")
}

// The compose project name is the directory compose runs from: `ubuntu` on the
// deployed host, `local_docker` under local_deploy.sh. Parsing the name would
// resolve one and 404 the other, so the service comes from the label.
func TestGetContainers_ServiceFromComposeLabel(t *testing.T) {
	mock := containerFixture()
	mock.queryResponses[listQuery] = vectorResponse(
		listResult("local_docker-mithril-1", "mithril", "ghcr.io/muchq/mithril:abc123", 100),
	)
	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	assert.Equal(t, "mithril", response.Containers[0].Service)
}

// Falls back to parsing when cAdvisor isn't storing container labels.
func TestGetContainers_ServiceFallsBackToName(t *testing.T) {
	mock := containerFixture()
	mock.queryResponses[listQuery] = vectorResponse(
		Result{
			Metric: map[string]string{"name": "ubuntu-mithril-1", "image": "ghcr.io/muchq/mithril:abc123"},
			Value:  []interface{}{1609459200.0, "100"},
		},
	)
	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	assert.Equal(t, "mithril", response.Containers[0].Service)
}

// House contract: a failing scrape source yields an empty section with 200, so
// the page shape stays stable. `reporting` is what keeps that unambiguous.
func TestGetContainers_PrometheusDown(t *testing.T) {
	handler := NewMetricsHandler(&mockPrometheusClient{queryError: assert.AnError})
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), `"containers":[]`, "empty slice, not null")
}

// ---------------------------------------------------------------- detail

func TestGetContainerDetail(t *testing.T) {
	tests := []struct {
		name       string
		requested  string
		wantStatus int
		wantName   string
	}{
		{"by service name", "posterize", http.StatusOK, "ubuntu-posterize-1"},
		{"by container name", "ubuntu-posterize-1", http.StatusOK, "ubuntu-posterize-1"},
		{"infrastructure container", "caddy", http.StatusOK, "ubuntu-caddy-1"},
		{"unknown", "nope", http.StatusNotFound, ""},
		{"substring of a real one", "d4", http.StatusNotFound, ""},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			handler := NewMetricsHandler(containerFixture())
			req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/"+tt.requested, nil)
			req.SetPathValue("name", tt.requested)
			w := httptest.NewRecorder()
			handler.GetContainerDetail(w, req)

			require.Equal(t, tt.wantStatus, w.Code)
			if tt.wantStatus != http.StatusOK {
				return
			}
			var got ContainerDetail
			require.NoError(t, json.Unmarshal(w.Body.Bytes(), &got))
			assert.Equal(t, tt.wantName, got.Container.Name)
			assert.False(t, got.Timestamp.IsZero(), "point-in-time payloads carry a timestamp")
		})
	}
}

// The payload, not just the name: returning the right name attached to another
// container's stats would otherwise pass.
func TestGetContainerDetail_Payload(t *testing.T) {
	handler := NewMetricsHandler(containerFixture())
	req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/posterize", nil)
	req.SetPathValue("name", "posterize")
	w := httptest.NewRecorder()
	handler.GetContainerDetail(w, req)

	require.Equal(t, http.StatusOK, w.Code)
	var got ContainerDetail
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &got))
	assert.True(t, got.Container.CrashLooping)
	assert.Equal(t, 47.0, got.Container.RestartsLastHour)
	assert.Equal(t, 8.0, got.Container.UptimeSeconds)
	assert.Equal(t, "abc123", got.Container.Version)
	assert.Equal(t, "posterize", got.Container.Service)

	// A healthy neighbour must not inherit any of that.
	req = httptest.NewRequest(http.MethodGet, "/metrics/v1/container/caddy", nil)
	req.SetPathValue("name", "caddy")
	w = httptest.NewRecorder()
	handler.GetContainerDetail(w, req)
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &got))
	assert.False(t, got.Container.CrashLooping)
	assert.Equal(t, "2-alpine", got.Container.Version)
	assert.Zero(t, got.Container.RestartsLastHour)
}

// The detail endpoint must scope every query to the one resolved container —
// a host-wide `name!=""` here would quietly restore per-request fan-out.
func TestGetContainerDetail_ScopedQueriesOnly(t *testing.T) {
	mock := containerFixture()
	handler := NewMetricsHandler(mock)
	req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/posterize", nil)
	req.SetPathValue("name", "posterize")
	w := httptest.NewRecorder()
	handler.GetContainerDetail(w, req)

	require.Equal(t, http.StatusOK, w.Code)
	assert.ElementsMatch(t, []string{
		listQuery,
		`sum by (name) (rate(container_cpu_usage_seconds_total{name="ubuntu-posterize-1"}[5m]))*100`,
		`sum by (name) (rate(container_cpu_cfs_throttled_seconds_total{name="ubuntu-posterize-1"}[5m]))`,
		`max by (name) (container_memory_usage_bytes{name="ubuntu-posterize-1"})`,
		`max by (name) (container_spec_memory_limit_bytes{name="ubuntu-posterize-1"})`,
		`sum by (name) (rate(container_network_receive_bytes_total{name="ubuntu-posterize-1"}[5m]))`,
		`sum by (name) (rate(container_network_transmit_bytes_total{name="ubuntu-posterize-1"}[5m]))`,
		`max by (name) (changes(container_start_time_seconds{name="ubuntu-posterize-1"}[1h]))`,
		`time()-max by (name) (container_start_time_seconds{name="ubuntu-posterize-1"})`,
		`time()-max by (name) (container_last_seen{name="ubuntu-posterize-1"})`,
		`sum by (name) (increase(container_oom_events_total{name="ubuntu-posterize-1"}[1h]))`,
	}, mock.instantQueries)
}

// A single-resource lookup can't degrade to an empty answer: without the
// listing there's no way to tell a missing container from an unknown one, and
// answering 404 would claim it doesn't exist.
func TestGetContainerDetail_PrometheusDown(t *testing.T) {
	handler := NewMetricsHandler(&mockPrometheusClient{queryError: assert.AnError})
	req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/posterize", nil)
	req.SetPathValue("name", "posterize")
	w := httptest.NewRecorder()
	handler.GetContainerDetail(w, req)
	assert.Equal(t, http.StatusInternalServerError, w.Code)
}

// ---------------------------------------------------------------- timeseries

func TestGetContainerTimeSeries(t *testing.T) {
	tests := []struct {
		name       string
		requested  string
		timeRange  string
		wantStatus int
	}{
		{"by service name", "posterize", "1d", http.StatusOK},
		{"by container name", "ubuntu-posterize-1", "30m", http.StatusOK},
		{"unknown container", "nope", "1d", http.StatusNotFound},
		{"invalid range", "posterize", "99y", http.StatusBadRequest},
		// Range is validated before the container is resolved, so a request
		// that is wrong twice reports the range rather than a 404.
		{"unknown container and invalid range", "nope", "99y", http.StatusBadRequest},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			mock := containerFixture()
			mock.queryRangeResponse = &QueryResponse{}
			handler := NewMetricsHandler(mock)
			req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/"+tt.requested+"/timeseries/"+tt.timeRange, nil)
			req.SetPathValue("name", tt.requested)
			req.SetPathValue("range", tt.timeRange)
			w := httptest.NewRecorder()
			handler.GetContainerTimeSeries(w, req)

			assert.Equal(t, tt.wantStatus, w.Code)
			if tt.wantStatus == http.StatusOK {
				var response TimeSeriesResponse
				require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
				assert.Equal(t, tt.timeRange, response.TimeRange)
			}
			if tt.wantStatus != http.StatusOK {
				assert.NotContains(t, w.Body.String(), `"series"`)
			}
		})
	}
}

// Keyed on the exact queries the handler should issue, so building them from
// the requested name instead of the resolved one — or dropping a series
// entirely — fails here rather than shipping empty charts.
func TestGetContainerTimeSeries_QueriesResolvedName(t *testing.T) {
	name := "ubuntu-posterize-1"
	mock := containerFixture()
	mock.queryRangeResponses = map[string]*QueryResponse{
		fmt.Sprintf(`rate(container_cpu_usage_seconds_total{name="%s"}[5m])*100`, name):       rangeResponse("x"),
		fmt.Sprintf(`container_memory_usage_bytes{name="%s"}`, name):                          rangeResponse("x"),
		fmt.Sprintf(`sum(rate(container_network_receive_bytes_total{name="%s"}[5m]))`, name):  rangeResponse("x"),
		fmt.Sprintf(`sum(rate(container_network_transmit_bytes_total{name="%s"}[5m]))`, name): rangeResponse("x"),
		// 1d -> step 5m, and the restart window must match the step.
		fmt.Sprintf(`changes(container_start_time_seconds{name="%s"}[5m])`, name): rangeResponse("x"),
		fmt.Sprintf(`time()-container_start_time_seconds{name="%s"}`, name):       rangeResponse("x"),
	}
	handler := NewMetricsHandler(mock)

	// Addressed by service; every query must still use the container name.
	req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/posterize/timeseries/1d", nil)
	req.SetPathValue("name", "posterize")
	req.SetPathValue("range", "1d")
	w := httptest.NewRecorder()
	handler.GetContainerTimeSeries(w, req)

	require.Equal(t, http.StatusOK, w.Code)
	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	assert.Empty(t, mock.misses, "every range query should have matched a fixture entry")
	assert.Len(t, response.Series, 6)
	names := map[string]bool{}
	for _, s := range response.Series {
		names[s.MetricName] = true
		assert.NotEmpty(t, s.Values, "points actually flowed through")
	}
	assert.True(t, names["restarts"], "the series this endpoint exists for")
	assert.True(t, names["uptime_seconds"])
	assert.True(t, names["cpu_usage"])
	assert.Equal(t, "5m", response.Step)
	assert.True(t, response.EndTime.After(response.StartTime))
}

// At 7d the step is 1h, so a fixed [5m] window would inspect only the last 5
// minutes of each hour and drop most restarts — the overnight crash loop this
// endpoint is meant to reveal.
func TestGetContainerTimeSeries_RestartWindowTracksStep(t *testing.T) {
	name := "ubuntu-posterize-1"
	mock := containerFixture()
	mock.queryRangeResponses = map[string]*QueryResponse{
		fmt.Sprintf(`changes(container_start_time_seconds{name="%s"}[1h])`, name): rangeResponse("x"),
	}
	handler := NewMetricsHandler(mock)
	req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/posterize/timeseries/7d", nil)
	req.SetPathValue("name", "posterize")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()
	handler.GetContainerTimeSeries(w, req)

	require.Equal(t, http.StatusOK, w.Code)
	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "1h", response.Step)
	found := false
	for _, s := range response.Series {
		if s.MetricName == "restarts" {
			found = true
		}
	}
	assert.True(t, found, "restarts queried with a [1h] window at the 1h step")
}

func TestGetContainerTimeSeries_PrometheusDown(t *testing.T) {
	handler := NewMetricsHandler(&mockPrometheusClient{queryError: assert.AnError})
	req := httptest.NewRequest(http.MethodGet, "/metrics/v1/container/posterize/timeseries/1d", nil)
	req.SetPathValue("name", "posterize")
	req.SetPathValue("range", "1d")
	w := httptest.NewRecorder()
	handler.GetContainerTimeSeries(w, req)
	assert.Equal(t, http.StatusInternalServerError, w.Code)
}

// Partial failure is where the reporting guard earns its keep: restarts came
// back but uptime didn't, so isCrashLooping(47, 0) would say "crash looping"
// off a zero we never actually measured. Claiming either state from
// half the data is worse than saying we don't know.
func TestGetContainers_PartialDataIsNotAVerdict(t *testing.T) {
	mock := containerFixture()
	mock.queryResponses[listQuery] = vectorResponse(
		listResult("ubuntu-mithril-1", "mithril", "ghcr.io/muchq/mithril:abc123", 100),
	)
	mock.queryResponses[groupedRestartsQuery] = vectorResponse(vectorResult("ubuntu-mithril-1", "47"))
	// mithril deliberately absent from the uptime vector

	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	assert.False(t, response.Containers[0].Reporting)
	assert.False(t, response.Containers[0].CrashLooping, "no verdict without uptime")
	assert.Equal(t, 47.0, response.Containers[0].RestartsLastHour, "what we did measure is still reported")
}

// cAdvisor keeps answering for a container after it stops — the series lingers
// until retention drops it — so the age of the last_seen stamp is what
// separates "running" from "gone", in the window where uptime has nothing to
// say because there is no current run to measure.
func TestGetContainers_LastSeenAge(t *testing.T) {
	mock := containerFixture()
	mock.queryResponses[listQuery] = vectorResponse(
		listResult("ubuntu-mithril-1", "mithril", "ghcr.io/muchq/mithril:abc123", 100),
	)
	mock.queryResponses[groupedLastSeenQuery] = vectorResponse(vectorResult("ubuntu-mithril-1", "240"))

	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	assert.Equal(t, 240.0, response.Containers[0].LastSeenAgoSeconds)
}

func TestGetContainers_OOMEvents(t *testing.T) {
	mock := containerFixture()
	mock.queryResponses[listQuery] = vectorResponse(
		listResult("ubuntu-mithril-1", "mithril", "ghcr.io/muchq/mithril:abc123", 100),
	)
	mock.queryResponses[groupedUptimeQuery] = vectorResponse(vectorResult("ubuntu-mithril-1", "3600"))
	mock.queryResponses[groupedOOMQuery] = vectorResponse(vectorResult("ubuntu-mithril-1", "2"))

	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	assert.Equal(t, 2.0, response.Containers[0].OOMEventsLastHour)
}

// Some cAdvisor builds ship no OOM counter at all. Its absence leaves the field
// zero, which reads identically to "no kills" — so it must not touch Reporting,
// which is the field that does carry a verdict about visibility.
func TestGetContainers_OOMCounterAbsentIsNotAVerdict(t *testing.T) {
	mock := containerFixture()
	mock.queryResponses[listQuery] = vectorResponse(
		listResult("ubuntu-mithril-1", "mithril", "ghcr.io/muchq/mithril:abc123", 100),
	)
	mock.queryResponses[groupedUptimeQuery] = vectorResponse(vectorResult("ubuntu-mithril-1", "3600"))
	// mithril deliberately absent from the OOM vector

	handler := NewMetricsHandler(mock)
	w := httptest.NewRecorder()
	handler.GetContainers(w, httptest.NewRequest(http.MethodGet, "/metrics/v1/containers", nil))

	require.Equal(t, http.StatusOK, w.Code)
	var response ContainerMetrics
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.Len(t, response.Containers, 1)
	assert.Zero(t, response.Containers[0].OOMEventsLastHour)
	assert.True(t, response.Containers[0].Reporting, "a missing OOM counter says nothing about visibility")
}
