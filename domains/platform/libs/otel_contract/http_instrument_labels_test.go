package otel_contract

import (
	"regexp"
	"sort"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The label-set row of the contract (#1305): which labels each rail puts on
// the shared http_server_* instruments.
//
// prom_proxy's standard block relies on two properties that no single
// language's tests can see together. The counters and the histogram carry
// {http_method, route, service_name} — the route being what lets probeFilter
// subtract probe traffic from every Serving number — and the in-flight gauge
// carries {http_method, service_name} with NO route, which is what makes the
// same negative matcher safe on the gauge selector: a negative matcher also
// matches series without the label, so a gauge that grew a route on one rail
// would have its in-flight probes subtracted there and nowhere else.
//
// Timing rides along, in prose because it is pinned behaviorally per rail
// (yodel's HttpServerMetricsTest, server_pal's http_metrics_label_tests,
// futility's http_metrics_test): the counters and the histogram move at
// request completion, where the matched route and the status exist; only the
// gauge moves at start, which is why it is the instrument without a route.
// The `requests` counter therefore counts completed-or-abandoned requests on
// every rail — the same totals, observed a request-duration later.
//
// futility alone adds labels beyond the shared core, and those are pinned
// here too rather than left as an undocumented fork: {status_code, result}
// on the duration histogram, {error_type, status_code, result} on the
// failure counter. Queries in prom_proxy aggregate over them (sum / sum by
// (le)), so they cost cardinality but not correctness; a rail quietly
// growing or losing an extra label should fail this file, not surface as a
// dashboard mystery.

// The shared core, spelled once.
var (
	routedCoreLabels = []string{"http_method", "route", "service_name"}
	gaugeCoreLabels  = []string{"http_method", "service_name"}
)

// labelSet is a sorted, deduplicated list of label keys.
func labelSet(keys ...string) []string {
	seen := map[string]bool{}
	var out []string
	for _, key := range keys {
		if !seen[key] {
			seen[key] = true
			out = append(out, key)
		}
	}
	sort.Strings(out)
	return out
}

// keysIn lifts every submatch of pattern out of one matched region of source.
func keysIn(t *testing.T, source []byte, region *regexp.Regexp, key *regexp.Regexp, what string) []string {
	t.Helper()
	match := region.FindSubmatch(source)
	require.NotNil(t, match,
		"no %s found. If the declaration was reshaped, update the pattern here: an unparseable "+
			"rail is one this test stops checking.", what)
	var keys []string
	for _, sub := range key.FindAllSubmatch(match[1], -1) {
		keys = append(keys, string(sub[1]))
	}
	require.NotEmpty(t, keys, "parsed no label keys out of the %s", what)
	return labelSet(keys...)
}

// --- futility (C++), http_metrics.cc ---

var (
	// Anchored on the qualified definition sites — the call sites inside
	// Record* would otherwise match first and capture arbitrary text.
	cppGaugeAttrsBody = regexp.MustCompile(
		`HttpMetricsManager::CreateGaugeAttributes\([^)]*\)\s*const\s*\{\s*return\s*\{([^;]*)\};`)
	cppBaseAttrsBody = regexp.MustCompile(
		`(?s)HttpMetricsManager::CreateBaseAttributes\([^)]*\)\s*const\s*\{.*?return\s*\{([^;]*)\};`)
	cppRequestAttrsBody = regexp.MustCompile(
		`(?s)HttpMetricsManager::CreateRequestAttributes\([^)]*\)\s*const\s*\{([^}]*)\}`)
	cppPairKey  = regexp.MustCompile(`\{"([a-z_]+)"`)
	cppExtraKey = regexp.MustCompile(`attrs\["([a-z_]+)"\]`)
)

func futilityLabelSets(t *testing.T) (gauge, routed, histogramExtras, failureExtras []string) {
	t.Helper()
	source := codeLines(t, "../futility/otel/http_metrics.cc", "http_server_requests")

	gauge = keysIn(t, source, cppGaugeAttrsBody, cppPairKey, "futility gauge attribute set")
	routed = keysIn(t, source, cppBaseAttrsBody, cppPairKey, "futility base attribute set")

	// CreateRequestAttributes adds onto the base set with attrs["..."]
	// assignments; the failure arm adds onto that with failure_attrs["..."].
	histogramExtras = keysIn(t, source, cppRequestAttrsBody, cppExtraKey,
		"futility request-attribute extras")

	failureArm := regexp.MustCompile(
		`(?s)auto failure_attrs = CreateRequestAttributes[^;]*;(\s*failure_attrs\[[^;]*;)+`)
	match := failureArm.FindSubmatch(source)
	require.NotNil(t, match, "no failure_attrs arm found in http_metrics.cc; update this pattern")
	for _, sub := range regexp.MustCompile(`failure_attrs\["([a-z_]+)"\]`).FindAllSubmatch(match[0], -1) {
		failureExtras = append(failureExtras, string(sub[1]))
	}
	require.NotEmpty(t, failureExtras, "parsed no failure-arm extras out of http_metrics.cc")
	failureExtras = labelSet(append(failureExtras, histogramExtras...)...)
	return gauge, routed, histogramExtras, failureExtras
}

// --- server_pal (Rust), lib.rs ---

var (
	rustGaugeAttrs = regexp.MustCompile(`(?s)let gauge_attrs = \[(.*?)\];`)
	rustRouteAttrs = regexp.MustCompile(`(?s)let route_attrs = \[(.*?)\];`)
	rustKey        = regexp.MustCompile(`KeyValue::new\("([a-z_]+)"`)
)

// --- yodel (Java), OtlpJsonEncoder.java ---

var (
	javaGaugeOverload = regexp.MustCompile(
		`(?s)addPointAttributes\(ObjectNode point, String serviceName, String httpMethod\) \{(.*?)\}`)
	javaRoutedOverload = regexp.MustCompile(
		`(?s)addPointAttributes\(\s*ObjectNode point, String serviceName, String httpMethod, String route\) \{(.*?)\}`)
	javaKey = regexp.MustCompile(`addAttribute\(attributes, "([a-z_]+)"`)
)

func TestSharedLabelSetsMatchAcrossAllThreeEmitters(t *testing.T) {
	futilityGauge, futilityRouted, _, _ := futilityLabelSets(t)

	rustSource := codeLines(t, "../server_pal/src/lib.rs", "http_server_requests")
	rustGauge := keysIn(t, rustSource, rustGaugeAttrs, rustKey, "server_pal gauge_attrs binding")
	rustRouted := keysIn(t, rustSource, rustRouteAttrs, rustKey, "server_pal route_attrs binding")

	javaSource := codeLines(t,
		"../yodel/src/main/java/com/muchq/platform/yodel/OtlpJsonEncoder.java",
		"http_server_requests")
	javaGauge := keysIn(t, javaSource, javaGaugeOverload, javaKey, "yodel gauge addPointAttributes overload")
	javaRouted := keysIn(t, javaSource, javaRoutedOverload, javaKey, "yodel routed addPointAttributes overload")

	wantRouted := labelSet(routedCoreLabels...)
	wantGauge := labelSet(gaugeCoreLabels...)

	for rail, got := range map[string][]string{
		"futility/otel (C++)": futilityRouted,
		"server_pal (Rust)":   rustRouted,
		"yodel (Java)":        javaRouted,
	} {
		assert.Equal(t, wantRouted, got,
			"%s labels its counters/histogram with %v, not the shared core %v. The three rails "+
				"must move together: prom_proxy's probeFilter and its Probes tiles select on "+
				"`route`, scoped by `service_name` (#1305).", rail, got, wantRouted)
	}

	for rail, got := range map[string][]string{
		"futility/otel (C++)": futilityGauge,
		"server_pal (Rust)":   rustGauge,
		"yodel (Java)":        javaGauge,
	} {
		assert.Equal(t, wantGauge, got,
			"%s labels its in-flight gauge with %v, not %v. The gauge deliberately carries no "+
				"route — it moves at request start, before routing — and a route label on one "+
				"rail's gauge would make probeFilter's negative matcher subtract in-flight "+
				"probes there and nowhere else.", rail, got, wantGauge)
	}
}

// futility's extras, pinned as the deliberate dialect they are. If these grow
// or shrink, the change should be a decision recorded here (and in yodel's
// README dialect table), not an accident.
func TestFutilityDialectExtrasAreExactlyTheDocumentedOnes(t *testing.T) {
	_, _, histogramExtras, failureExtras := futilityLabelSets(t)
	assert.Equal(t, labelSet("result", "status_code"), histogramExtras,
		"futility's duration histogram extras changed; prom_proxy aggregates over them today, "+
			"but they are part of the documented dialect")
	assert.Equal(t, labelSet("error_type", "result", "status_code"), failureExtras,
		"futility's failure-counter extras changed; the error_type breakdown "+
			"(rate_limited/client_error/server_error) is the part services alert on")
}

// The sentinel and the health literal are one vocabulary across the rails:
// prom_proxy subtracts route="/health" everywhere, and every rail parks
// unmatched requests under the same spelling so a fleet-wide "unmatched
// traffic" query means one thing.
func TestRouteSentinelAndHealthLiteralAgreeAcrossRails(t *testing.T) {
	sentinels := []struct {
		path        string
		marker      string
		declaration *regexp.Regexp
	}{
		{
			path:        "../yodel/src/main/java/com/muchq/platform/yodel/micronaut/HttpServerMetricsFilter.java",
			marker:      "UNMATCHED_ROUTE",
			declaration: regexp.MustCompile(`UNMATCHED_ROUTE = "([a-z_]+)"`),
		},
		{
			path:        "../server_pal/src/lib.rs",
			marker:      "UNMATCHED_ROUTE",
			declaration: regexp.MustCompile(`UNMATCHED_ROUTE: &str = "([a-z_]+)"`),
		},
		{
			path:        "../aura/middleware.h",
			marker:      "kUnmatchedRoute",
			declaration: regexp.MustCompile(`kUnmatchedRoute\[\] = "([a-z_]+)"`),
		},
	}
	for _, sentinel := range sentinels {
		source := codeLines(t, sentinel.path, sentinel.marker)
		match := sentinel.declaration.FindSubmatch(source)
		require.NotNil(t, match,
			"no unmatched-route sentinel found in %s; if it was renamed, rename it here too",
			sentinel.path)
		assert.Equal(t, "unmatched", string(match[1]),
			"%s spells the sentinel %q; the rails agreed on \"unmatched\" (#1303, #1305)",
			sentinel.path, match[1])
	}

	auraSource := codeLines(t, "../aura/middleware.h", "kHealthRoute")
	health := regexp.MustCompile(`kHealthRoute\[\] = "(/[a-z]+)"`).FindSubmatch(auraSource)
	require.NotNil(t, health, "no kHealthRoute literal found in aura/middleware.h")
	assert.Equal(t, "/health", string(health[1]),
		"aura's health route is %q; prom_proxy subtracts and charts exactly route=\"/health\", "+
			"and the compose healthchecks probe the same literal (#1307)", health[1])
}
