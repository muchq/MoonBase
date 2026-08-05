package otel_contract

import (
	"os"
	"regexp"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The route-vocabulary literals of the label contract (#1305): the unmatched
// sentinel and the health route are one spelling across the three rails.
//
// This file deliberately pins only what no single language's tests can see —
// three constants in three languages that must be byte-equal. Everything else
// about the label sets is pinned per rail against real exported payloads,
// which is strictly stronger than a source regex: server_pal's
// http_metrics_label_tests read data-point attributes off an in-memory
// exporter, futility's http_metrics_test asserts whole attribute maps through
// CapturingMetricsRecorder, and yodel's OtlpJsonEncoderTest asserts the OTLP
// JSON with containsExactly. (An earlier draft of this file also re-modeled
// each rail's attribute composition in Go regexes; the export tests replace
// it — a regex over source can agree with the code while disagreeing with
// the wire, which is the failure mode this package exists to prevent.)
//
// Why these literals matter: prom_proxy subtracts route="/health" from every
// Serving number and selects it on each service's Probes tile, and a
// fleet-wide "unmatched traffic" query only means one thing if every rail
// parks unrouted requests under the same spelling. The timing half of the
// contract rides in prose because it, too, is pinned behaviorally per rail
// (yodel's HttpServerMetricsTest, server_pal's abandonment test, futility's
// http_metrics_test): the counters and the histogram move at request
// completion, where the matched route and the status exist; only the
// in-flight gauge moves at start, which is why it is the instrument without
// a route on every rail.

// codeLines is the file with comment lines blanked, plus a guard that the
// file still contains a marker the caller's pins rely on — a file that
// stopped mentioning it is one these tests silently stopped checking.
func codeLines(t *testing.T, path, marker string) []byte {
	t.Helper()
	source, err := os.ReadFile(path)
	require.NoError(t, err, "cannot read %s — has the data dependency been dropped?", path)
	source = withoutCommentLines(source)
	require.Contains(t, string(source), marker,
		"%s no longer mentions %q; this pin is reading the wrong file (or the wrong shape of "+
			"the right one)", path, marker)
	return source
}

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
