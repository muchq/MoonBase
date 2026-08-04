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

// The #1303 guard: every standard Serving expression excludes the probe
// route, per selector — a healthcheck's steady GET /health otherwise floors
// every request count and drags every latency figure toward its
// sub-millisecond durations, indistinguishable from real traffic. The gauge
// included: a negative matcher also matches series with no route label at
// all, so the filter subtracts futility's route-labeled gauge and passes
// yodel's route-less one through whole — safe on every selector.
func TestRegistry_StandardServingQueriesExcludeTheProbeRoute(t *testing.T) {
	// Per http_server_* selector, not per query: error_rate_percent names
	// three selectors, and a Contains over the whole expression passes while
	// two of them still count probes.
	selectorPattern := regexp.MustCompile(`http_server_[a-z_]+\{[^}]*\}`)
	check := func(t *testing.T, what, query string) {
		t.Helper()
		selectors := selectorPattern.FindAllString(query, -1)
		assert.NotEmpty(t, selectors, "%s has no http_server selector: %s", what, query)
		for _, selector := range selectors {
			assert.Contains(t, selector, `route!="/health"`,
				"%s counts probe traffic in %q: %s", what, selector, query)
		}
	}
	for _, name := range serviceOrder {
		for _, q := range standardScalarQueries(name) {
			check(t, "scalar for "+name, q.Query)
		}
		for key, query := range standardTimeseriesQueries(name, "5m") {
			check(t, "timeseries "+key+" for "+name, query)
		}
	}
}

// The UI classifies series by name: anything in its STANDARD_SERIES
// mirror renders on the standard charts, so a custom key that collides
// is silently reclassified and never charts. The registry comment asks
// authors to avoid this; this pins it.
func TestRegistry_CustomTimeseriesKeysDoNotShadowStandardSeries(t *testing.T) {
	for _, name := range serviceOrder {
		standard := standardTimeseriesQueries(name, "5m")
		// Expanded panel keys, not the def map's own keys: a toggleable
		// entry's map key ("command") never appears in a response — only its
		// _rate/_count panels do, and those are what could collide.
		for key := range expandCustomTimeseries(serviceRegistry[name].CustomTimeseries, "5m") {
			_, shadows := standard[key]
			assert.False(t, shadows, "service %q custom series %q shadows a standard series", name, key)
		}
	}
}

// Two CustomTimeseries entries can collide on their own without ever
// naming a standard series: a fixed def literally named "foo_rate" beside
// a toggleable def with base "foo" both expand to the same "foo_rate"
// panel. expandCustomTimeseries's map assignment is last-wins on that, so
// one def's query silently overwrites the other's rather than erroring —
// exactly the footgun portrait's cache_hit_rate (fixed) sits next to were
// anyone to add a toggleable "cache_hit" counter beside it.
//
// Counting keys in vs. panels out is what catches this: a keys() diff can't
// tell "two defs produced the same key" from "one key, as expected", but a
// collision always drops the total panel count below the sum of what each
// def contributes on its own.
func TestRegistry_CustomTimeseriesPanelKeysAreUniquePerService(t *testing.T) {
	for _, name := range serviceOrder {
		custom := serviceRegistry[name].CustomTimeseries
		wantPanels := 0
		for key, def := range custom {
			wantPanels += len(def.panels(key, "5m"))
		}
		gotPanels := len(expandCustomTimeseries(custom, "5m"))
		assert.Equal(t, wantPanels, gotPanels,
			"service %q has two CustomTimeseries entries whose panels collide", name)
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

// The other shape a selector can take: {__name__=~"a|b",labels}. One
// expression covering two metric names has to be written this way, which
// portrait's operations tile now is — a count and a rate form built from one
// selector over both cache counters.
var nameMatcherPattern = regexp.MustCompile(`\{[^}]*__name__=~"([^"]*)"[^}]*\}`)

type cacheSelector struct {
	name   string
	labels string
	raw    string
}

// Every cache_* selector in a query, in either shape. The label block is
// reported the same way for both, because in both it is the same braces —
// only where the metric name sits differs.
//
// The __name__ form is consumed first and blanked out before the bare-name
// pattern runs. Without that the names inside the quoted alternation match as
// if they were bare selectors, and report no labels at all: the check would
// then fail on the one selector that is in fact fully scoped.
func cacheSelectorsIn(query string) []cacheSelector {
	var found []cacheSelector
	remainder := query

	for _, match := range nameMatcherPattern.FindAllStringSubmatch(query, -1) {
		for _, name := range strings.Split(match[1], "|") {
			if !strings.HasPrefix(name, "cache_") {
				continue
			}
			found = append(found, cacheSelector{name: name, labels: match[0], raw: match[0]})
		}
		remainder = strings.Replace(remainder, match[0], "{}", 1)
	}

	for _, loc := range cacheSelectorPattern.FindAllStringSubmatchIndex(remainder, -1) {
		// A cache_* token followed by '=' is a label name, not a metric name:
		// scene complexity is labelled cache_hit="false" (#1287), and matching
		// that would report a selector with no labels and fail every scoping
		// check below. RE2 has no lookahead, so the position is checked here
		// rather than in the pattern.
		//
		// Narrow on purpose. Skipping anything that merely *contains* a label
		// matcher would skip the real selectors too, since cache="trace" sits
		// inside every one of them.
		if end := loc[1]; end < len(remainder) && remainder[end] == '=' {
			continue
		}
		whole := remainder[loc[0]:loc[1]]
		labels := ""
		if loc[2] >= 0 {
			labels = remainder[loc[2]:loc[3]]
		}
		found = append(found, cacheSelector{
			name:   strings.SplitN(whole, "{", 2)[0],
			labels: labels,
			raw:    whole,
		})
	}
	return found
}

// Every expression a service's entry can produce, scalars in both views plus
// the timeseries. Counter-derived tiles keep their expression in Counter
// rather than Query, so a scan of Query alone would silently skip them.
func allQueriesFor(entry serviceEntry) []string {
	var queries []string
	for _, def := range entry.CustomScalars {
		queries = append(queries, def.AllQueries()...)
	}
	for _, query := range expandCustomTimeseries(entry.CustomTimeseries, "5m") {
		queries = append(queries, query)
	}
	return queries
}

// Portrait's cache queries follow the emitter: aura::Cache emits the
// standard cache family labeled by service and cache, so the bespoke
// trace_cache_* series no longer exist to be queried. A rename on one side
// only leaves the dashboard reading a metric nothing writes — which looks
// exactly like a cache that is never used.
func TestRegistry_PortraitCacheQueriesUseTheStandardFamily(t *testing.T) {
	portrait := serviceRegistry["portrait"]

	// The panels the UI actually receives: cache_hit_rate is fixed-form and
	// keeps its def-map key; cache_operations is toggleable, so its def-map
	// key ("cache_operations") never reaches a response — only its expanded
	// _rate/_count panels do.
	require.Contains(t, portrait.CustomTimeseries, "cache_hit_rate")
	panels := expandCustomTimeseries(portrait.CustomTimeseries, "5m")
	require.Contains(t, panels, "cache_operations_rate")
	require.Contains(t, panels, "cache_operations_count")

	queries := allQueriesFor(portrait)
	require.NotEmpty(t, queries)

	seen := map[string]int{}
	for _, query := range queries {
		assert.NotContains(t, query, "trace_cache_",
			"query still reads the deleted bespoke series: %s", query)

		// Per selector, not per query. A hit-rate query names both counters,
		// so asserting the query as a whole contains the labels passes while
		// either half of it is unscoped — and an unscoped half silently sums
		// every service's caches into portrait's panel.
		for _, selector := range cacheSelectorsIn(query) {
			seen[selector.name]++
			assert.Contains(t, selector.labels, `service_name="portrait"`,
				"selector %q is not scoped to portrait, in %s", selector.raw, query)
			assert.Contains(t, selector.labels, `cache="trace"`,
				"selector %q does not name which cache, in %s", selector.raw, query)
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
		for _, query := range allQueriesFor(serviceRegistry[name]) {
			for _, selector := range cacheSelectorsIn(query) {
				assert.Contains(t, selector.labels, "service_name=",
					"service %q has an unscoped cache selector %q", name, selector.raw)
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
	assert.Contains(t, all,
		`sum(rate(http_server_requests_total{service_name="golf_hub",route!="/health"}[5m]))`)
	assert.Contains(t, all,
		`histogram_quantile(0.95,sum by (le) (rate(http_server_request_duration_microseconds_bucket{service_name="golf_hub",route!="/health"}[5m])))`)
	assert.Contains(t, all,
		`sum(http_server_requests_active_gauge{service_name="golf_hub",route!="/health"})`)
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
		responses[def.QueryFor(DefaultView)] = scalarResponse(fmt.Sprintf("%d", 200+i))
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

	// An unasked-for view is the default, and it is stated rather than left
	// for the client to assume.
	assert.Equal(t, string(DefaultView), response.View)

	// Custom groups keep registry order and every descriptor is present.
	require.Len(t, response.Custom, 3)
	assert.Equal(t, "Sessions", response.Custom[0].Title)
	assert.Equal(t, "Activity", response.Custom[1].Title)
	assert.Equal(t, "Chat", response.Custom[2].Title)
	assert.Len(t, response.Custom[0].Metrics, 7)
	assert.Len(t, response.Custom[1].Metrics, 4)
	require.Len(t, response.Custom[2].Metrics, 5)

	// A gauge: one form, so no toggle offered.
	assert.Equal(t, CustomMetricValue{Label: "active", Value: 200.0, Unit: "sessions"},
		response.Custom[0].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "commands", Value: 207.0, Toggleable: true},
		response.Custom[1].Metrics[0])
	assert.Equal(t, CustomMetricValue{Label: "rejections", Value: 209.0, Toggleable: true},
		response.Custom[1].Metrics[2])
	// Chat starts at CustomScalars index 11, so its first value is 200+11.
	assert.Equal(t, CustomMetricValue{Label: "messages", Value: 211.0, Toggleable: true},
		response.Custom[2].Metrics[0])
	// The omitted query's descriptor survives with a zero value.
	last := response.Custom[2].Metrics[4]
	assert.Equal(t, omitted.Label, last.Label)
	assert.Equal(t, 0.0, last.Value)
}

func TestMetricsHandler_GetServiceMetrics_NoCustomServiceKeepsEmptyArray(t *testing.T) {
	mockClient := &mockPrometheusClient{
		queryResponses: map[string]*QueryResponse{
			`sum(rate(http_server_requests_total{service_name="mithril",route!="/health"}[5m]))`: scalarResponse("2.5"),
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
	// The seven standard series plus golf's nineteen custom panels: three
	// fixed-form charts, and eight toggleable ones each expanding to a
	// _rate/_count pair.
	expected := []string{
		"request_rate", "request_count", "error_rate_percent", "error_count",
		"avg_duration_us", "p95_duration_us", "active_requests",
		"sessions_active", "session_starts", "chat_catch_up_rows",
		"command_rate", "command_count",
		"event_rate", "event_count",
		"rejection_rate", "rejection_count",
		"disconnect_rate", "disconnect_count",
		"rate_limited_rate", "rate_limited_count",
		"chat_message_rate", "chat_message_count",
		"chat_delivery_rate", "chat_delivery_count",
		"chat_failure_rate", "chat_failure_count",
	}
	assert.Len(t, names, len(expected))
	for _, name := range expected {
		assert.True(t, names[name], name)
	}
}

// --- The Serving chart's count/rate pair --------------------------------

// Both literal pins, the same way TestStandardQueries_GoldenStrings pins the
// scalar block: a typo in the shared instrument name should fail loudly
// rather than hide behind the enumeration in TestRegistry_*.
//
// request_count windows by step (5m here, standing in for whatever
// GetTimeRangeConfig picked) rather than the scalar tiles' fixed 5m — see
// standardTimeseriesQueries for why a fixed window would overlap and
// double-count. request_rate is untouched: unlike the scalar block's
// counter-derived custom tiles, this pair doesn't share a query built by
// switching a function name, precisely so neither series' meaning depends on
// a query parameter — see the same comment for why.
func TestStandardTimeseriesQueries_RequestRateAndCountAreIndependentSeries(t *testing.T) {
	queries := standardTimeseriesQueries("golf_hub", "5m")
	assert.Equal(t,
		`sum(rate(http_server_requests_total{service_name="golf_hub",route!="/health"}[5m]))`,
		queries["request_rate"])
	assert.Equal(t,
		`sum(increase(http_server_requests_total{service_name="golf_hub",route!="/health"}[5m]))`,
		queries["request_count"])

	// A different step changes request_count's window but not request_rate's
	// — that one is fixed at 5m regardless of how far apart the chart's
	// points are.
	withStep := standardTimeseriesQueries("golf_hub", "30s")
	assert.Equal(t,
		`sum(increase(http_server_requests_total{service_name="golf_hub",route!="/health"}[30s]))`,
		withStep["request_count"])
	assert.Equal(t, queries["request_rate"], withStep["request_rate"])
}

// error_rate_percent is itself a ratio of two rates with no count form of its
// own, but the failure counter it's built from does — error_count is that
// counter's own count/rate pair, the same relationship request_count has to
// request_rate, not "the error rate as a count".
func TestStandardTimeseriesQueries_ErrorCountIsTheFailureCounterNotTheRatio(t *testing.T) {
	queries := standardTimeseriesQueries("golf_hub", "5m")
	assert.Equal(t,
		`sum(increase(http_server_requests_failure_total{service_name="golf_hub",route!="/health"}[5m]))`,
		queries["error_count"])

	withStep := standardTimeseriesQueries("golf_hub", "30s")
	assert.Equal(t,
		`sum(increase(http_server_requests_failure_total{service_name="golf_hub",route!="/health"}[30s]))`,
		withStep["error_count"])
	// error_rate_percent itself never changes: it isn't wrapped in rate()/
	// increase() over a counter the way request_rate/error_count are, so
	// there's no window to swap out.
	assert.Equal(t, queries["error_rate_percent"], withStep["error_rate_percent"])
}

// avg/p95 latency and active requests have no counter behind them at all — a
// ratio-of-rates, a quantile, and a gauge — so unlike the two request/error
// pairs they get no count-form sibling.
func TestStandardTimeseriesQueries_LatencyAndActiveHaveNoCountForm(t *testing.T) {
	queries := standardTimeseriesQueries("portrait", "5m")
	for _, key := range []string{"avg_duration_us", "p95_duration_us", "active_requests"} {
		assert.NotContains(t, queries, key+"_count", "%s unexpectedly grew a count-form sibling", key)
	}
}

func TestMetricsHandler_GetServiceMetricsTimeSeries_RequestCountBucketsByTheRangesStep(t *testing.T) {
	countQuery := `sum(increase(http_server_requests_total{service_name="golf_hub",route!="/health"}[1h]))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				countQuery: rangeResponse("request_count"),
			},
		},
	}

	// The 7d range steps at 1h (models.go's GetTimeRangeConfig), which
	// request_count's window has to match — a mismatch here means the mock's
	// exact-string lookup misses and the series comes back empty, catching a
	// step that was hardcoded instead of threaded through.
	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/7d", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var requestCount *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "request_count" {
			requestCount = &response.Series[i]
		}
	}
	require.NotNil(t, requestCount, "request_count missing from the response")
	require.Len(t, requestCount.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
}

// The failure-counter analogue of the request_count test above: error_count
// windows by the range's own step too, and it's a different selector
// (http_server_requests_failure_total, not _total) from request_count, so
// a copy-paste that left it reading the wrong counter would still pass a
// test that only checked the window.
func TestMetricsHandler_GetServiceMetricsTimeSeries_ErrorCountBucketsByTheRangesStep(t *testing.T) {
	countQuery := `sum(increase(http_server_requests_failure_total{service_name="golf_hub",route!="/health"}[1h]))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				countQuery: rangeResponse("error_count"),
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/7d", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var errorCount *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "error_count" {
			errorCount = &response.Series[i]
		}
	}
	require.NotNil(t, errorCount, "error_count missing from the response")
	require.Len(t, errorCount.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
}

// No ?view= on this route at all: request_rate and request_count both come
// back on every request, unconditionally, regardless of query parameters —
// there is nothing here to reject as invalid.
func TestMetricsHandler_GetServiceMetricsTimeSeries_IgnoresViewParam(t *testing.T) {
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{queryRangeResponse: rangeResponse("series")},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/1d?view=cumulative", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "1d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	names := map[string]bool{}
	for _, series := range response.Series {
		names[series.MetricName] = true
	}
	assert.True(t, names["request_rate"])
	assert.True(t, names["request_count"])
}

// --- Custom Trends charts, extended with the same count/rate pairing -------

// A toggleable custom chart expands to exactly two panels, keyed off the
// def-map key rather than spelling either suffix in the descriptor — the
// same shape standardRequestRateQuery's sibling gets, so a Trends chart and
// the Serving chart can't drift onto different conventions.
func TestCustomTimeseriesDef_ToggleableExpandsToRateAndCountBucketedByStep(t *testing.T) {
	def := tsCounter(`stream_commands_total`)
	panels := def.panels("command", "30s")
	assert.Equal(t, map[string]string{
		"command_rate":  `sum(rate(stream_commands_total[5m]))`,
		"command_count": `sum(increase(stream_commands_total[30s]))`,
	}, panels)

	// The rate form is fixed at the scalar tiles' 5m regardless of step —
	// only the count form windows per point.
	withStep := def.panels("command", "1h")
	assert.Equal(t, panels["command_rate"], withStep["command_rate"])
	assert.NotEqual(t, panels["command_count"], withStep["command_count"])
}

// A fixed custom chart keeps its map key unchanged and ignores step
// entirely — there's no window in it to bucket.
func TestCustomTimeseriesDef_FixedFormKeepsItsOwnKey(t *testing.T) {
	def := tsFixed(`sum(stream_sessions_active_gauge)`)
	panels := def.panels("sessions_active", "30s")
	assert.Equal(t, map[string]string{"sessions_active": `sum(stream_sessions_active_gauge)`}, panels)
}

// End to end through the handler: a toggleable custom chart's two panels
// both land in the response, and the count form is bucketed by the range's
// own step exactly like request_count.
func TestMetricsHandler_GetServiceMetricsTimeSeries_CustomCounterPanelBucketsByStep(t *testing.T) {
	countQuery := `sum(increase(stream_commands_total[1h]))`
	handler := &MetricsHandler{
		promClient: &mockPrometheusClient{
			queryRangeResponses: map[string]*QueryResponse{
				countQuery: rangeResponse("command_count"),
			},
		},
	}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub/timeseries/7d", nil)
	req.SetPathValue("name", "golf_hub")
	req.SetPathValue("range", "7d")
	w := httptest.NewRecorder()

	handler.GetServiceMetricsTimeSeries(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response TimeSeriesResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))

	var commandCount *TimeSeries
	for i := range response.Series {
		if response.Series[i].MetricName == "command_count" {
			commandCount = &response.Series[i]
		}
	}
	require.NotNil(t, commandCount, "command_count missing from the response")
	require.Len(t, commandCount.Values, 2, "the mock's fixture didn't answer — the handler built a different query than expected")
}

// microgpt-serve's tokens_per_second baked one form into its name the way
// request_rate used to; "tokens" replaces it so the chart can toggle. No
// other file names the old key (grep confirmed it before the rename), so
// this is the only place a caller could still expect it.
func TestRegistry_MicrogptTokensReplacesTokensPerSecond(t *testing.T) {
	panels := expandCustomTimeseries(serviceRegistry["microgpt-serve"].CustomTimeseries, "5m")
	assert.Contains(t, panels, "tokens_rate")
	assert.Contains(t, panels, "tokens_count")
	assert.NotContains(t, panels, "tokens_per_second")
	assert.Equal(t,
		`sum(rate(microgpt_tokens_generated_total[5m]))`,
		panels["tokens_rate"])
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
		// Both views of a toggleable tile. They share a selector, so a scope
		// or name error appears in both — but reading them from AllQueries is
		// what keeps the counter tiles in scope at all.
		labelled["scalar "+def.Label] = def.AllQueries()
	}
	// Both panels of a toggleable timeseries entry, for the same reason.
	for key, query := range expandCustomTimeseries(entry.CustomTimeseries, "5m") {
		labelled["timeseries "+key] = []string{query}
	}

	seen := map[string]int{}
	for what, queries := range labelled {
		for _, query := range queries {
			// The probes tile (#1303) is the one entry that reads the standard
			// http_server family rather than an IndexWorker instrument: it
			// shows the healthcheck traffic probeFilter subtracts from every
			// Serving number. It still has to be scoped and route-selected.
			if strings.Contains(query, "http_server_requests_total") {
				assert.Contains(t, query, `service_name="one_d4"`,
					"%s reads the standard family unscoped: %s", what, query)
				assert.Contains(t, query, `route="/health"`,
					"%s reads the standard family without naming the probe route,"+
						" so it double-counts serving traffic: %s", what, query)
				continue
			}
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
		joined += strings.Join(def.AllQueries(), "\n") + "\n"
	}
	for _, query := range expandCustomTimeseries(entry.CustomTimeseries, "5m") {
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
		for _, query := range def.AllQueries() {
			assert.NotRegexp(t, `outcome\s*!=|outcome\s*!~`, query,
				"scalar %s selects outcomes by exclusion", def.Label)
		}
	}
	for key, query := range expandCustomTimeseries(entry.CustomTimeseries, "5m") {
		assert.NotRegexp(t, `outcome\s*!=|outcome\s*!~`, query,
			"timeseries %s selects outcomes by exclusion", key)
	}

	// Exclusion is not the only way back to a wrong failure line, and the check above only
	// forbids the one spelling. Reverting to outcome=~"failed|interrupted|lease_lost", or to a
	// bare sum over index_runs_total with no outcome at all, uses no negation and passes it. So
	// the set is pinned positively, the way the duration guard pins outcome="completed".
	runFailure, ok := entry.CustomTimeseries["run_failure"]
	require.True(t, ok, "one_d4 lost its run_failure timeseries")
	require.True(t, runFailure.toggleable(), "run_failure stopped being counter-derived")
	outcomeMatch := regexp.MustCompile(`outcome=~?"([^"]*)"`).FindStringSubmatch(runFailure.Counter)
	require.NotNil(t, outcomeMatch,
		"run_failure names no outcome at all, so every run counts as a failure: %s",
		runFailure.Counter)
	assert.ElementsMatch(t, []string{"failed", "interrupted"},
		strings.Split(outcomeMatch[1], "|"),
		"run_failure must name exactly failed|interrupted — lease_lost is ordinary and puts a"+
			" permanent floor under the line, and anything wider counts healthy runs as"+
			" failures: %s", runFailure.Counter)
}

// --- The count/rate toggle (#1287) ------------------------------------------

func TestMetricsHandler_GetServiceMetrics_RateViewSelectsTheRateForm(t *testing.T) {
	entry := serviceRegistry["golf_hub"]
	responses := map[string]*QueryResponse{}
	for i, def := range entry.CustomScalars {
		responses[def.QueryFor(ViewRate)] = scalarResponse(fmt.Sprintf("%d", 300+i))
	}

	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryResponses: responses}}
	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub?view=rate", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)
	assert.Equal(t, http.StatusOK, w.Code)

	var response ServiceMetricsResponse
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Equal(t, "rate", response.View)

	byLabel := map[string]CustomMetricValue{}
	for _, group := range response.Custom {
		for _, metric := range group.Metrics {
			byLabel[metric.Label] = metric
		}
	}

	// A unitless counter reads per-second; one with a unit keeps it and gains
	// the suffix. Both come from the same descriptor, which in the count view
	// answers "" and "rows".
	assert.Equal(t, "/s", byLabel["commands"].Unit)
	assert.Equal(t, "rows/s", byLabel["delivered_rows"].Unit)
	assert.Equal(t, 307.0, byLabel["commands"].Value)

	// The fixed-form tiles are untouched by the view: a gauge has no rate, and
	// the windowed mean is already a ratio of two rates.
	assert.Equal(t, "sessions", byLabel["active"].Unit)
	assert.False(t, byLabel["active"].Toggleable)
	assert.Equal(t, "rows", byLabel["catch_up_rows_avg_5m"].Unit)
	assert.False(t, byLabel["catch_up_rows_avg_5m"].Toggleable)
}

func TestMetricsHandler_GetServiceMetrics_InvalidViewIsRejected(t *testing.T) {
	handler := &MetricsHandler{}

	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub?view=cumulative", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)

	// Rejected rather than quietly defaulted. "cumulative" is the specific
	// wrong answer someone will reach for, and defaulting would hand them a
	// windowed count while they believed they were reading a lifetime total.
	assert.Equal(t, http.StatusBadRequest, w.Code)

	var response map[string]interface{}
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &response))
	assert.Contains(t, response["detail"], "Invalid view")
}

// No tile reads a counter cumulatively any more (#1287).
//
// sum(x) over a monotonic counter is its value since the process started, so
// it drops to zero on every deploy and cannot be compared across one. The
// dashboard read twenty-one tiles that way. increase() answers the same
// question and survives a restart.
//
// Gauges are exempt and have to be: sum(http_server_requests_active_gauge) is
// the correct form for a level. The rule is about counters, which is what the
// _total suffix marks.
func TestRegistry_NoTileReadsACounterCumulatively(t *testing.T) {
	bareCounterSum := regexp.MustCompile(`sum\(\s*[a-zA-Z_]*_total`)

	check := func(t *testing.T, what, query string) {
		t.Helper()
		if !strings.Contains(query, "_total") {
			return
		}
		assert.NotRegexp(t, bareCounterSum, query,
			"%s sums a counter directly, so it resets on deploy: %s", what, query)
		assert.True(t,
			strings.Contains(query, "rate(") || strings.Contains(query, "increase("),
			"%s reads a _total counter outside any rate() or increase(): %s", what, query)
	}

	for _, name := range serviceOrder {
		entry := serviceRegistry[name]
		for _, def := range entry.CustomScalars {
			for _, query := range def.AllQueries() {
				check(t, "scalar "+name+"/"+def.Label, query)
			}
		}
		for key, query := range expandCustomTimeseries(entry.CustomTimeseries, "5m") {
			check(t, "timeseries "+name+"/"+key, query)
		}
		for _, q := range standardScalarQueries(name) {
			check(t, "standard scalar "+name, q.Query)
		}
		for key, query := range standardTimeseriesQueries(name, "5m") {
			check(t, "standard timeseries "+name+"/"+key, query)
		}
	}
}

// The standard block's requests tile, pinned as a literal.
//
// It is the one converted tile whose JSON key could not change: StandardMetrics
// is a fixed struct the UI reads by name, and the UI lives in another repo. So
// requests_total now holds a windowed count under a name that still says total,
// and only this assertion says which of the two it is.
func TestStandardQueries_RequestsIsWindowedNotCumulative(t *testing.T) {
	var requests string
	for _, q := range standardScalarQueries("golf_hub") {
		var probe StandardMetrics
		if q.Field(&probe) == &probe.RequestsTotal {
			requests = q.Query
		}
	}
	require.NotEmpty(t, requests, "no query maps to RequestsTotal")
	assert.Equal(t,
		`sum(increase(http_server_requests_total{service_name="golf_hub",route!="/health"}[5m]))`,
		requests)
}

// Counters that answer "has this happened" rather than "how fast" count over a
// day.
//
// Over five minutes an interruption from an hour ago reads zero, and a tile
// whose whole purpose is to be non-zero after something went wrong disarms
// itself between the failure and someone looking at it.
func TestRegistry_AlarmCountersCountOverALongWindow(t *testing.T) {
	// First, that the window is actually long. Every check below compares
	// against the alarmWindow constant, so the constant itself is the thing
	// that has to be pinned — otherwise shrinking it leaves every assertion
	// comparing against the shrunken value and still passing, which is the
	// regression these tiles exist to prevent, invisible to the test meant to
	// catch it.
	//
	// A floor rather than "not the default": alarmWindow = "6m" differs from
	// the default and still fails the purpose, since a failure from an hour
	// ago has already decayed out of it. An hour is the loosest bound that
	// still means "someone who looks after the fact sees it".
	window, err := time.ParseDuration(alarmWindow)
	require.NoError(t, err, "alarmWindow is not a duration Go can parse: %q", alarmWindow)
	require.GreaterOrEqual(t, window, time.Hour,
		"alarmWindow is %s — long enough to differ from the default, too short to still be "+
			"non-zero when someone reads the dashboard after the failure", alarmWindow)

	alarms := map[string][]string{
		"one_d4":   {"runs_failed", "runs_interrupted", "runs_lease_lost"},
		"golf_hub": {"failures"},
	}

	for service, labels := range alarms {
		byLabel := map[string]customScalarDef{}
		for _, def := range serviceRegistry[service].CustomScalars {
			byLabel[def.Label] = def
		}
		for _, label := range labels {
			def, ok := byLabel[label]
			require.True(t, ok, "%s lost its %s tile", service, label)
			require.True(t, def.Toggleable(), "%s/%s stopped being counter-derived", service, label)
			assert.Equal(t, alarmWindow, def.window(),
				"%s/%s is an alarm and must not decay inside a five-minute window", service, label)
			assert.Contains(t, def.QueryFor(ViewCount), "["+alarmWindow+"]")
		}
	}
}

// A toggleable tile's label may not name one of its two forms.
//
// The label is what the dashboard prints beside the number. games_indexed_total
// showing a per-second rate, or commands_per_sec showing a count, is a caption
// that contradicts the value under it — and it is the exact state the registry
// was in before the toggle, when the form was fixed at declaration time.
func TestRegistry_ToggleableLabelsNameNoForm(t *testing.T) {
	for _, name := range serviceOrder {
		for _, def := range serviceRegistry[name].CustomScalars {
			if !def.Toggleable() {
				continue
			}
			assert.NotContains(t, def.Label, "_total",
				"%s/%s names the count form but also has a rate form", name, def.Label)
			assert.NotContains(t, def.Label, "_per_sec",
				"%s/%s names the rate form but also has a count form", name, def.Label)
		}
	}
}

// Every counter tile builds both forms from one selector, and the two differ
// only by the function wrapping it.
//
// Spelling the two expressions out separately in the descriptor is the obvious
// alternative, and the failure it invites is silent: the count and rate tiles
// drift onto different label selectors and the toggle starts switching between
// two different questions rather than two views of one.
func TestRegistry_BothViewsShareOneSelector(t *testing.T) {
	for _, name := range serviceOrder {
		for _, def := range serviceRegistry[name].CustomScalars {
			if !def.Toggleable() {
				assert.Equal(t, def.Query, def.QueryFor(ViewRate),
					"%s/%s is fixed-form but changed under a view", name, def.Label)
				continue
			}
			count := def.QueryFor(ViewCount)
			rate := def.QueryFor(ViewRate)
			assert.NotEqual(t, count, rate)
			assert.Equal(t, strings.Replace(count, "increase(", "rate(", 1), rate,
				"%s/%s builds its two views from different expressions", name, def.Label)
			assert.Contains(t, count, def.Counter)
			assert.Contains(t, rate, def.Counter)
		}
	}
}

// The two new JSON keys, asserted on the raw payload rather than through the
// struct.
//
// Every other handler test unmarshals into ServiceMetricsResponse, which means
// it reads the keys back through the same tags it wrote them with — renaming
// `json:"view"` to `json:"metric_view"` would keep all of them green while the
// UI, which lives in another repo and reads by name, silently loses the field.
// The same reasoning already covers requests_total's *value*; these are the
// keys it applies to.
func TestMetricsHandler_GetServiceMetrics_JsonKeysAreStable(t *testing.T) {
	entry := serviceRegistry["golf_hub"]
	responses := map[string]*QueryResponse{}
	for i, def := range entry.CustomScalars {
		responses[def.QueryFor(DefaultView)] = scalarResponse(fmt.Sprintf("%d", 400+i))
	}

	handler := &MetricsHandler{promClient: &mockPrometheusClient{queryResponses: responses}}
	req := httptest.NewRequest("GET", "/metrics/v1/service/golf_hub", nil)
	req.SetPathValue("name", "golf_hub")
	w := httptest.NewRecorder()

	handler.GetServiceMetrics(w, req)
	require.Equal(t, http.StatusOK, w.Code)

	var raw map[string]interface{}
	require.NoError(t, json.Unmarshal(w.Body.Bytes(), &raw))
	assert.Equal(t, "count", raw["view"], `the response must carry a top-level "view" key`)

	groups, ok := raw["custom"].([]interface{})
	require.True(t, ok, "custom is not an array")
	require.NotEmpty(t, groups)

	tiles := map[string]map[string]interface{}{}
	for _, group := range groups {
		for _, tile := range group.(map[string]interface{})["metrics"].([]interface{}) {
			metric := tile.(map[string]interface{})
			tiles[metric["label"].(string)] = metric
		}
	}

	counter, ok := tiles["commands"]
	require.True(t, ok, "golf_hub lost its commands tile")
	assert.Equal(t, true, counter["toggleable"],
		`a counter tile must carry "toggleable": true for the UI to offer the switch`)

	// And the negative half: omitempty means a fixed-form tile has no key at
	// all, which is what tells the UI not to draw a toggle it cannot honour.
	gauge, ok := tiles["active"]
	require.True(t, ok, "golf_hub lost its active tile")
	_, present := gauge["toggleable"]
	assert.False(t, present,
		"a fixed-form tile must omit the key entirely, not send false: %v", gauge)
}
