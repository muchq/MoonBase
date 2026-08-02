// Package otel_contract holds cross-language pins for the metric contract the
// three emitters share.
//
// Nothing imports it. It exists because prom_proxy asks one PromQL question of
// Java, Rust and C++ services alike, and several of the properties that makes
// correct are properties no single language's test can see.
package otel_contract

import (
	"os"
	"regexp"
	"strconv"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// Where each rail declares the HTTP latency bucket boundaries, and the pattern
// that lifts the literal out of it. Paths are relative to this package, which
// is where rules_go runs the test from; a dropped data dependency shows up as
// a read error rather than a skipped rail.
var rails = []struct {
	name    string
	path    string
	pattern *regexp.Regexp
}{
	{
		name:    "yodel (Java)",
		path:    "../yodel/src/main/java/com/muchq/platform/yodel/HttpServerMetrics.java",
		pattern: regexp.MustCompile(`BUCKET_BOUNDS\s*=\s*\{([^}]*)\}`),
	},
	{
		name:    "futility/otel (C++)",
		path:    "../futility/otel/otel_provider.h",
		pattern: regexp.MustCompile(`kHttpLatencyBucketBoundsMicros\s*=\s*\{([^}]*)\}`),
	},
	{
		name:    "server_pal (Rust)",
		path:    "../server_pal/src/lib.rs",
		pattern: regexp.MustCompile(`HTTP_LATENCY_BUCKET_BOUNDS_MICROS\s*:\s*\[f64;\s*\d+\]\s*=\s*\[([^\]]*)\]`),
	},
}

var numberPattern = regexp.MustCompile(`\d+(?:\.\d+)?`)

// The layout the three emitters used to share, and the reason #1286 exists:
// the OTel SDK defaults, which are shaped for milliseconds. Read as
// microseconds the top bound is 10ms, so every slower request lands in +Inf
// and histogram_quantile answers a flat 10000 forever.
var otelSdkMillisecondDefaults = []float64{
	0, 5, 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000, 7500, 10000,
}

func boundsFrom(t *testing.T, path string, pattern *regexp.Regexp) []float64 {
	t.Helper()
	source, err := os.ReadFile(path)
	require.NoError(t, err, "cannot read %s — has the data dependency been dropped?", path)

	match := pattern.FindSubmatch(source)
	require.NotNil(t, match,
		"no bucket-bounds declaration found in %s. If the constant was renamed, rename it here "+
			"too: an unparseable rail is one this test stops checking.", path)

	var bounds []float64
	for _, literal := range numberPattern.FindAllString(string(match[1]), -1) {
		value, err := strconv.ParseFloat(literal, 64)
		require.NoError(t, err)
		bounds = append(bounds, value)
	}
	require.NotEmpty(t, bounds, "parsed an empty bucket set out of %s", path)
	return bounds
}

// The three emitters declare the same HTTP latency buckets.
//
// prom_proxy charts p95 for every service through one histogram_quantile
// expression, whatever language the service is written in. Quantiles are read
// off bucket counts, so that expression only compares like with like while the
// layouts match — one rail widening alone makes its services quietly
// incomparable to the rest on a chart that still renders.
//
// This is the guard #1286 asked for. Before it, the only thing keeping the
// three aligned was a comment in each asking the next person not to touch it.
func TestHttpLatencyBucketsMatchAcrossAllThreeEmitters(t *testing.T) {
	byRail := map[string][]float64{}
	for _, rail := range rails {
		byRail[rail.name] = boundsFrom(t, rail.path, rail.pattern)
	}
	require.Len(t, byRail, len(rails), "two rails share a name")

	reference := rails[0]
	want := byRail[reference.name]
	for _, rail := range rails[1:] {
		assert.Equal(t, want, byRail[rail.name],
			"%s and %s declare different HTTP latency buckets. They have to move together — "+
				"prom_proxy's p95 query spans all three, and a mismatched layout makes those "+
				"services incomparable on one chart.", reference.name, rail.name)
	}
}

// And that the shared layout is the fixed one, not the broken one.
//
// Equality alone is satisfied by all three reverting together, which is
// exactly what a well-meaning "restore the SDK defaults" change would look
// like. These are the properties that make the layout right for microseconds.
func TestHttpLatencyBucketsAreShapedForMicroseconds(t *testing.T) {
	for _, rail := range rails {
		t.Run(rail.name, func(t *testing.T) {
			bounds := boundsFrom(t, rail.path, rail.pattern)

			assert.NotEqual(t, otelSdkMillisecondDefaults, bounds,
				"%s is back on the OTel SDK defaults. Those are millisecond-shaped; this emitter "+
					"records microseconds, so the top bound means 10ms and p95 pins there (#1286).",
				rail.name)

			for i := 1; i < len(bounds); i++ {
				require.Greater(t, bounds[i], bounds[i-1],
					"%s bounds must ascend: %v then %v", rail.name, bounds[i-1], bounds[i])
			}

			assert.Positive(t, bounds[0],
				"%s starts at or below zero, wasting the first bucket on values no duration takes",
				rail.name)

			// One second in microseconds. portrait was observed serving at a
			// real p95 near that (#1287) against a 10ms ceiling, so a top bound
			// below it puts the same class of traffic back in the overflow
			// bucket that hid the defect the first time.
			assert.GreaterOrEqual(t, bounds[len(bounds)-1], 1_000_000.0,
				"%s tops out at %v microseconds, which a slow request will exceed — and "+
					"histogram_quantile answers the top finite bound when it does",
				rail.name, bounds[len(bounds)-1])
		})
	}
}
