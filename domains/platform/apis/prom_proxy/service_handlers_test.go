package prom_proxy

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func rangeResponse(metricName string) *QueryResponse {
	return &QueryResponse{
		Status: "success",
		Data: struct {
			ResultType string   `json:"resultType"`
			Result     []Result `json:"result"`
		}{
			ResultType: "matrix",
			Result: []Result{
				{
					Metric: map[string]string{"__name__": metricName},
					Values: [][]interface{}{
						{1609459200.0, "25.5"},
						{1609459230.0, "26.1"},
					},
				},
			},
		},
	}
}

func TestRegistry_OrderAndEntriesAgree(t *testing.T) {
	assert.Len(t, serviceOrder, len(serviceRegistry))
	for _, name := range serviceOrder {
		_, ok := serviceRegistry[name]
		assert.True(t, ok, "serviceOrder entry %q missing from registry", name)
	}
}

// The UI classifies series by name: anything in its STANDARD_SERIES
// mirror renders on the standard charts, so a custom key that collides
// is silently reclassified and never charts. The registry comment asks
// authors to avoid this; this pins it.
func TestRegistry_CustomTimeseriesKeysDoNotShadowStandardSeries(t *testing.T) {
	for _, name := range serviceOrder {
		standard := standardTimeseriesQueries(name)
		for key := range serviceRegistry[name].CustomTimeseries {
			_, shadows := standard[key]
			assert.False(t, shadows, "service %q custom series %q shadows a standard series", name, key)
		}
	}
}

// Any reference to a cache metric, with whatever label selector follows it.
//
// Deliberately broader than cache_hits_total|cache_misses_total: a selector
// the pattern does not match is a selector that escapes the label checks
// below entirely, so a half-finished rename (cache_misses, or a
// cache_evictions_total nobody emits) would slip through unexamined rather
// than being flagged.
var cacheSelectorPattern = regexp.MustCompile(`\bcache_[a-z_]+\b(\{[^}]*\})?`)

// Portrait's cache queries follow the emitter: aura::Cache emits the
// standard cache family labeled by service and cache, so the bespoke
// trace_cache_* series no longer exist to be queried. A rename on one side
// only leaves the dashboard reading a metric nothing writes — which looks
// exactly like a cache that is never used.
func TestRegistry_PortraitCacheQueriesUseTheStandardFamily(t *testing.T) {
	portrait := serviceRegistry["portrait"]

	// The panels the UI renders by key.
	require.Contains(t, portrait.CustomTimeseries, "cache_hit_rate")
	require.Contains(t, portrait.CustomTimeseries, "cache_operations_rate")

	var queries []string
	for _, def := range portrait.CustomScalars {
		queries = append(queries, def.Query)
	}
	for _, query := range portrait.CustomTimeseries {
		queries = append(queries, query)
	}
	require.NotEmpty(t, queries)

	seen := map[string]int{}
	for _, query := range queries {
		assert.NotContains(t, query, "trace_cache_",
			"query still reads the deleted bespoke series: %s", query)

		// Per selector, not per query. A hit-rate query names both counters,
		// so asserting the query as a whole contains the labels passes while
		// either half of it is unscoped — and an unscoped half silently sums
		// every service's caches into portrait's panel.
		for _, match := range cacheSelectorPattern.FindAllStringSubmatch(query, -1) {
			name := strings.SplitN(match[0], "{", 2)[0]
			seen[name]++
			labels := match[1]
			assert.Contains(t, labels, `service_name="portrait"`,
				"selector %q is not scoped to portrait, in %s", match[0], query)
			assert.Contains(t, labels, `cache="trace"`,
				"selector %q does not name which cache, in %s", match[0], query)
		}
	}

	// Both halves by name, not a bare count: the hit-rate panel divides one
	// by the sum of both, so a rename that left only the hits selector
	// standing would keep the count non-zero while the panel silently read a
	// series nothing writes.
	assert.NotZero(t, seen["cache_hits_total"], "nothing queries cache_hits_total")
	assert.NotZero(t, seen["cache_misses_total"], "nothing queries cache_misses_total")
	assert.Empty(t, mapKeysExcept(seen, "cache_hits_total", "cache_misses_total"),
		"portrait queries a cache series outside the standard family")
}

// Every cache selector in the whole registry has to name a service, not just
// portrait's. registry.go anticipates a second emitter, and an unscoped
// selector added by one sums every service's caches into that service's
// panel.
func TestRegistry_EveryCacheQueryNamesItsService(t *testing.T) {
	for _, name := range serviceOrder {
		entry := serviceRegistry[name]
		queries := make([]string, 0, len(entry.CustomScalars)+len(entry.CustomTimeseries))
		for _, def := range entry.CustomScalars {
			queries = append(queries, def.Query)
		}
		for _, query := range entry.CustomTimeseries {
			queries = append(queries, query)
		}
		for _, query := range queries {
			for _, match := range cacheSelectorPattern.FindAllStringSubmatch(query, -1) {
				assert.Contains(t, match[1], "service_name=",
					"service %q has an unscoped cache selector %q", name, match[0])
			}
		}
	}
}

func mapKeysExcept(m map[string]int, except ...string) []string {
	skip := map[string]bool{}
	for _, key := range except {
		skip[key] = true
	}
	var rest []string
	for key := range m {
		if !skip[key] {
			rest = append(rest, key)
		}
	}
	return rest
}

func TestMetricsHandler_GetServiceCatalog(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/services", nil)
	w := httptest.NewRecorder()

	handler.GetServiceCatalog(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var catalog ServiceCatalog
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &catalog))

	wantCustom := []struct {
		name      string
		hasCustom bool
	}{
		{"golf_hub", true},
		{"mcpserver", false},
		{"microgpt-serve", true},
		{"mithril", false},
		// The first Java service with a custom set (#1212): yodel grew counters and
		// distributions, so one_d4 can report indexing and motif work rather than
		// only the requests that asked for it.
		{"one_d4", true},
		{"portrait", true},
		{"posterize", false},
	}
	require.Len(t, catalog.Services, len(wantCustom))
	for i, want := range wantCustom {
		assert.Equal(t, want.name, catalog.Services[i].Name)
		assert.Equal(t, want.hasCustom, catalog.Services[i].HasCustom, want.name)
	}

	// The wire field names are the UI contract.
	assert.Contains(t, w.Body.String(), `"has_custom"`)
}

// A few standard queries pinned as literal strings, so a typo in the
// shared instrument names can't hide behind the enumeration below.
func TestStandardQueries_GoldenStrings(t *testing.T) {
	queries := standardScalarQueries("golf_hub")
	var all []string
	for _, q := range queries {
		all = append(all, q.Query)
	}
	assert.Contains(t, all, `sum(rate(http_server_requests_total{service_name="golf_hub"}[5m]))`)
	assert.Contains(t, all,
		`histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name="golf_hub"}[5m])))`)
	assert.Contains(t, all, `sum(http_server_requests_active_gauge{service_name="golf_hub"})`)
}

func TestMetricsHandler_GetServiceMetrics_MapsEveryFieldDistinctly(t *testing.T) {
	// Distinct value per query: a query wired to the wrong standard field
	// or custom descriptor fails loudly. One custom query is deliberately
	// omitted from the mock — its descriptor must still appear, zeroed.
	responses := map[string]*QueryResponse{}
	standard := standardScalarQueries("golf_hub")
	for i, q := range standard {
		responses[q.Query] = scalarResponse(fmt.Sprintf("%d", 100+i))
	}
	entry := serviceRegistry["golf_hub"]
	omitted := entry.CustomScalars[len(entry.CustomScalars)-1]
	for i, def := range entry.CustomScalars {
		if def == omitted {
			continue
		}
		responses[def.Query] = scalarResponse(fmt.Sprintf("%d", 200+i))
	}

	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryResponses: responses}}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "golf_hub", response.Service)
	assert.WithinDuration(t, time.Now(), response.Timestamp, 5*time.Second)

	// Standard fields in declaration order of standardScalarQueries.
	assert.Equal(t, 100.0, response.Standard.RequestsTotal)
	assert.Equal(t, 101.0, response.Standard.RatePerSec)
	assert.Equal(t, 102.0, response.Standard.SuccessCount5m)
	assert.Equal(t, 103.0, response.Standard.FailureCount5m)
	assert.Equal(t, 104.0, response.Standard.ErrorRatePercent)
	assert.Equal(t, 105.0, response.Standard.AvgDurationMicros)
	assert.Equal(t, 106.0, response.Standard.P95DurationMicros)
	assert.Equal(t, 107.0, response.Standard.ActiveRequests)

	// Custom groups keep registry order and every descriptor is present.
	require.Len(t, response.Custom, 3)
	assert.Equal(t, "Sessions", response.Custom[0].Title)
	assert.Equal(t, "Activity", response.Custom[1].Title)
	assert.Equal(t, "Chat", response.Custom[2].Title)
	assert.Len(t, response.Custom[0].Metrics, 6)
	assert.Len(t, response.Custom[1].Metrics, 4)
	require.Len(t, response.Custom[2].Metrics, 6)

	assert.Equal(t, CustomMetricValue{Label: "active", Value: 200.0, Unit: "sessions"},
		response.Custom[0].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "commands_per_sec", Value: 206.0, Unit: "/s"},
		response.Custom[1].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "rejections_per_sec", Value: 208.0, Unit: "/s"},
		response.Custom[1].Metrics[2])
	// Chat starts at CustomScalars index 10, so its first value is 200+10.
	assert.Equal(t, CustomMetricValue{Label: "messages_per_sec", Value: 210.0, Unit: "/s"},
		response.Custom[2].Metrics[0])
	// The omitted query's descriptor survives with a zero value.
	last := response.Custom[2].Metrics[5]
	assert.Equal(t, omitted.Label, last.Label)
	assert.Equal(t, 0.0, last.Value)
}

func TestMetricsHandler_GetServiceMetrics_NoCustomServiceKeepsEmptyArray(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryResponses: map[string]*QueryResponse{
			`sum(rate(http_server_requests_total{service_name="mithril"}[5m]))`: scalarResponse("2.5"),
		},
	}

	handler := &MetricsHandler{promClient: mockClient}

	req := httptest.NewRequest("GET", "/metrics/v1/service/mithril", nil)
	req.SetPathValue("name", "mithril")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	// The UI iterates custom unconditionally: the wire value must be [],
	// never null. Assert on the raw body — unmarshalling erases the
	// distinction.
	assert.Contains(t, w.Body.String(), `"custom":[]`)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, 2.5, response.Standard.RatePerSec)
}

func TestMetricsHandler_GetServiceMetrics_PrometheusError(t *testing.T) {
	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryError: assert.AnError}}

	req := httptest.NewRequest("GET", "/metrics/v1/service/portrait", nil)
	req.SetPathValue("name", "portrait")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	// Outage contract: zeroed values behind a stable page shape, never 500.
	assert.Equal(t, http.StatusOK, w.Code)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, 0.0, response.Standard.RatePerSec)
	require.Len(t, response.Custom, 2)
	assert.Equal(t, "Render cache", response.Custom[0].Title)
	assert.Equal(t, "Scene complexity", response.Custom[1].Title)
	for _, group := range response.Custom {
		for _, metric := range group.Metrics {
			assert.Equal(t, 0.0, metric.Value, metric.Label)
		}
	}
}

func TestMetricsHandler_GetServiceMetrics_UnknownService(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/service/nonesuch", nil)
	req.SetPathValue("name", "nonesuch")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)
	assert.Equal(t, http.StatusNotFound, w.Code)

	// Unknown service outranks an invalid range on the timeseries route.
	req = httptest.NewRequest("GET", "/metrics/v1/service/nonesuch/timeseries/bogus", nil)
	req.SetPathValue("name", "nonesuch")
	req.SetPathValue("range", "bogus")
	w = httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusNotFound, w.Code)
}

func TestMetricsHandler_GetServiceMetricsTimeSeries_InvalidRange(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/bogus", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "bogus")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)

	assert.Equal(t, http.StatusBadRequest, w.Code)

	var response map[string]interface{}
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Contains(t, response["detail"], "Invalid time range")
}

func TestMetricsHandler_GetServiceMetricsTimeSeries_StandardPlusCustom(t *testing.T) {
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{queryRangeResponse: rangeResponse("series")},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/30m", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "30m")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "30m", response.TimeRange)

	names := map[string]bool{}
	previous := ""
	for _, series := range response.Series {
		names[series.MetricName] = true
		// Deterministic payload order: the UI renders custom series in
		// payload order, so a map-iteration shuffle would reshuffle its
		// charts on every refresh.
		assert.GreaterOrEqual(t, series.MetricName, previous)
		previous = series.MetricName
	}
	// The five standard series plus golf's ten custom series.
	expected := []string{
		"request_rate", "error_rate_percent", "avg_duration_us", "p95_duration_us",
		"active_requests",
		"sessions_active", "session_starts", "command_rate", "event_rate",
		"rejection_rate", "disconnect_rate",
		"chat_message_rate", "chat_delivery_rate", "chat_failure_rate", "chat_catch_up_rows",
		"rate_limited_rate",
	}
	assert.Len(t, names, len(expected))
	for _, name := range expected {
		assert.True(t, names[name], name)
	}
}

func TestMetricsHandler_GetHostMetrics_Success(t *testing.T) {
	// One constant scalar response everywhere: the container-list query
	// yields a "caddy" row, and every follow-up scalar reads 42.5.
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryResponse: &QueryResponse{
				Status: "success",
				Data: struct {
					ResultType string   `json:"resultType"`
					Result     []Result `json:"result"`
				}{
					ResultType: "vector",
					Result: []Result{
						{
							Metric: map[string]string{"name": "caddy"},
							Value:  []interface{}{1609459200.0, "42.5"},
						},
					},
				},
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/host", nil)
	w := httptest.NewRecorder()

	handler.GetHostMetrics(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var response HostMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.NotNil(t, response.System)
	assert.Equal(t, 42.5, response.System.CPU.Utilization)
	require.Len(t, response.Containers, 1)
	assert.Equal(t, "caddy", response.Containers[0].Name)
	assert.Equal(t, 42.5, response.Containers[0].CPUUsagePercent)
	assert.WithinDuration(t, time.Now(), response.Timestamp, 5*time.Second)
}

func TestMetricsHandler_GetHostMetrics_PrometheusError(t *testing.T) {
	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryError: assert.AnError}}

	req := httptest.NewRequest("GET", "/metrics/v1/host", nil)
	w := httptest.NewRecorder()

	handler.GetHostMetrics(w, req)

	// Zeroed system, empty containers, 200 — the host page renders.
	assert.Equal(t, http.StatusOK, w.Code)

	var response HostMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	require.NotNil(t, response.System)
	assert.Equal(t, 0.0, response.System.CPU.Utilization)
	assert.Empty(t, response.Containers)
}

func TestMetricsHandler_GetHostMetricsTimeSeries(t *testing.T) {
	req := httptest.NewRequest("GET", "/metrics/v1/host/timeseries/bogus", nil)
	req.SetPathValue("range", "bogus")
	w := httptest.NewRecorder()

	handler := &MetricsHandler{}
	handler.GetHostMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusBadRequest, w.Code)

	handler = &MetricsHandler{
		promClient: &mockPrometheusClient{queryRangeResponse: rangeResponse("series")},
	}
	req = httptest.NewRequest("GET", "/metrics/v1/host/timeseries/30m", nil)
	req.SetPathValue("range", "30m")
	w = httptest.NewRecorder()

	handler.GetHostMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	hostSeries, containerSeries := 0, 0
	names := map[string]bool{}
	for _, series := range response.Series {
		names[series.MetricName] = true
		if strings.HasPrefix(series.MetricName, "container_") {
			containerSeries++
		} else {
			hostSeries++
		}
	}
	// Host series keep their names; container series are namespaced so
	// the merged payload can't collide.
	assert.True(t, names["cpu_utilization"])
	assert.True(t, names["container_cpu_usage"])
	// Restart history is what makes a crash loop visible after the fact.
	assert.True(t, names["container_restarts"])
	assert.Equal(t, 5, hostSeries)
	assert.Equal(t, 7, containerSeries)
}

// Every selector in the one_d4 set, checked one at a time.
//
// The first version of this joined every query into one blob and ran
// strings.Contains over it. That pins almost nothing: a prefix rename of every
// instrument passes (the old name is still a substring of the new one), dropping
// service_name from all seventeen selectors passes, deleting every timeseries
// passes, and pointing games_per_sec at index_runs_total passes — because a union
// of names never says which query uses which. The third of those is the exact
// failure the comment claimed to prevent.
//
// So this borrows the shape TestRegistry_PortraitCacheQueriesUseTheStandardFamily
// already uses: match each selector, assert on that selector, and close the set of
// names so a new instrument has to be declared here too.
var oneD4SelectorPattern = regexp.MustCompile(`\b((?:games_indexed|index_runs|index_months|chess_com_archive_fetches|motif_occurrences|index_run_duration_micros|index_games_per_month)[a-z_]*)(\{[^}]*\})?`)

// What IndexWorker emits, plus the suffixes the collector's Prometheus exporter
// appends: _total for a cumulative monotonic sum, _sum/_count for a histogram.
// The Java end is IndexWorkerTest#metrics_exportedInstrumentNames.
var oneD4ExportedNames = map[string]bool{
	"games_indexed_total":             true,
	"index_runs_total":                true,
	"index_months_total":              true,
	"chess_com_archive_fetches_total": true,
	"motif_occurrences_total":         true,
	"index_run_duration_micros_sum":   true,
	"index_run_duration_micros_count": true,
	"index_games_per_month_sum":       true,
	"index_games_per_month_count":     true,
}

func TestOneD4QueriesNameRealInstrumentsAndScopeThem(t *testing.T) {
	entry := serviceRegistry["one_d4"]
	require.NotEmpty(t, entry.CustomScalars)
	require.NotEmpty(t, entry.CustomTimeseries, "the timeseries panels are part of this entry")

	labelled := map[string][]string{}
	for _, def := range entry.CustomScalars {
		labelled["scalar "+def.Label] = []string{def.Query}
	}
	for key, query := range entry.CustomTimeseries {
		labelled["timeseries "+key] = []string{query}
	}

	seen := map[string]int{}
	for what, queries := range labelled {
		for _, query := range queries {
			matches := oneD4SelectorPattern.FindAllStringSubmatch(query, -1)
			assert.NotEmpty(t, matches, "%s queries no one_d4 instrument: %s", what, query)
			for _, match := range matches {
				name := match[1]
				seen[name]++
				assert.True(t, oneD4ExportedNames[name],
					"%s reads %q, which IndexWorker does not export", what, name)
				assert.Contains(t, match[2], `service_name="one_d4"`,
					"selector %q in %s is not scoped to one_d4, so it sums every service that ever"+
						" emits that name: %s", match[0], what, query)
			}
		}
	}

	// Closed in the other direction too: an instrument nothing charts is one the
	// service pays to export and nobody reads.
	for name := range oneD4ExportedNames {
		assert.NotZero(t, seen[name], "nothing in the one_d4 entry reads %s", name)
	}

	// Outcome labels are IndexWorker's vocabulary. A typo selects nothing rather
	// than erroring, so the tiles would read zero forever.
	joined := strings.Join([]string{}, "")
	for _, def := range entry.CustomScalars {
		joined += def.Query + "\n"
	}
	for _, query := range entry.CustomTimeseries {
		joined += query + "\n"
	}
	for _, label := range []string{`outcome="completed"`, `outcome="failed"`,
		`outcome="interrupted"`, `outcome="lease_lost"`, `result="empty"`, `result="cached"`} {
		assert.Contains(t, joined, label, "no query selects %s", label)
	}

	// The duration histogram carries the same outcome label the run counter does, and both
	// averages over it have to say which outcome they mean. Unscoped, a single run cut loose
	// at the six-hour MAX_RUN ceiling swamps every ordinary run in the window — the average
	// reads worst at the moment it is being used to judge how bad things are.
	for what, queries := range labelled {
		for _, query := range queries {
			for _, match := range oneD4SelectorPattern.FindAllStringSubmatch(query, -1) {
				if !strings.HasPrefix(match[1], "index_run_duration_micros") {
					continue
				}
				assert.Contains(t, match[2], `outcome="completed"`,
					"%s averages over every outcome, ceiling-length interrupts included: %s",
					what, query)
			}
		}
	}

	// And selected positively. outcome!="completed" reads as "everything that went wrong",
	// but IndexWorker's fourth outcome is lease_lost — a range changing hands because two
	// pollers overlapped, which is ordinary — so a negation puts a permanent floor under the
	// failure line. It also opts every future outcome in by default: whoever adds a fifth
	// label gets it counted as a failure without deciding that it is one.
	for _, def := range entry.CustomScalars {
		assert.NotRegexp(t, `outcome\s*!=|outcome\s*!~`, def.Query,
			"scalar %s selects outcomes by exclusion", def.Label)
	}
	for key, query := range entry.CustomTimeseries {
		assert.NotRegexp(t, `outcome\s*!=|outcome\s*!~`, query,
			"timeseries %s selects outcomes by exclusion", key)
	}

	// Exclusion is not the only way back to a wrong failure line, and the check above only
	// forbids the one spelling. Reverting to outcome=~"failed|interrupted|lease_lost", or to a
	// bare sum over index_runs_total with no outcome at all, uses no negation and passes it. So
	// the set is pinned positively, the way the duration guard pins outcome="completed".
	failureRate, ok := entry.CustomTimeseries["run_failure_rate"]
	require.True(t, ok, "one_d4 lost its run_failure_rate timeseries")
	outcomeMatch := regexp.MustCompile(`outcome=~?"([^"]*)"`).FindStringSubmatch(failureRate)
	require.NotNil(t, outcomeMatch,
		"run_failure_rate names no outcome at all, so every run counts as a failure: %s",
		failureRate)
	assert.ElementsMatch(t, []string{"failed", "interrupted"},
		strings.Split(outcomeMatch[1], "|"),
		"run_failure_rate must name exactly failed|interrupted — lease_lost is ordinary and puts a"+
			" permanent floor under the line, and anything wider counts healthy runs as"+
			" failures: %s", failureRate)
}
