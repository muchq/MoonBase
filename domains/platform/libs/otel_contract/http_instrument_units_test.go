package otel_contract

import (
	"os"
	"regexp"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The unit is the same cross-language contract as the description, and it
// fails more quietly (#1294). The collector's Prometheus exporter folds a
// non-empty unit into the metric NAME — a rail that set "us" on the duration
// histogram would export http_server_request_duration_microseconds_
// microseconds_{sum,count,bucket}. No conflict is logged anywhere: the series
// just forks, and prom_proxy selects every panel by literal metric name, so
// that service's tiles quietly empty out. The failure gives you nothing to
// grep for, which is why all three rails are pinned here to declaring no
// unit at all.
//
// The pins are shaped per rail because the rails declare units through
// different mechanisms:
//
//   - futility (C++) would pass a third argument to the Create* calls in
//     metrics.cc, so the pin is that every creation call has exactly the
//     (name, DescriptionFor(name)) shape. The exported behavior is pinned
//     in-language too, by http_instrument_descriptions_test.cc's
//     TheSharedInstrumentsDeclareNoUnit, which reads the descriptor off a
//     real export.
//   - server_pal (Rust) would chain .with_unit(...) onto an instrument
//     builder, so the pin is that lib.rs never calls it. The exported
//     behavior is pinned by lib.rs's no_shared_instrument_declares_a_unit,
//     through a real SDK pipeline.
//   - yodel (Java) hand-writes the OTLP JSON, where a unit is a "unit" key
//     on the metric node, so the pin is that OtlpJsonEncoder never writes
//     one. The encoder's own tests pin the exported JSON.
//
// Like every pin in this package, these read the source as text with the
// comments blanked, so prose about units (this paragraph's siblings in those
// files) cannot satisfy or trip them.

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

func TestServerPalDeclaresNoUnit(t *testing.T) {
	source := codeLines(t, "../server_pal/src/lib.rs", "http_server_requests")
	assert.NotContains(t, string(source), ".with_unit(",
		"server_pal chains .with_unit onto an instrument builder. The collector folds the unit "+
			"into the metric name and the service's series fork off every dashboard (#1294).")
}

func TestYodelWritesNoUnitKey(t *testing.T) {
	source := codeLines(t,
		"../yodel/src/main/java/com/muchq/platform/yodel/OtlpJsonEncoder.java",
		"http_server_requests")
	assert.NotRegexp(t, regexp.MustCompile(`put\(\s*"unit"`), string(source),
		"OtlpJsonEncoder writes a \"unit\" key into the OTLP payload. The collector folds the "+
			"unit into the metric name and the service's series fork off every dashboard (#1294).")
}

// Every instrument-creation call in the C++ recorder passes exactly a name
// and its canonical description — no third (unit) argument. The shape is
// pinned tightly enough that reshaping the call fails the parse guard rather
// than silently exempting the rail.
var cppCreateCall = regexp.MustCompile(
	`Create(?:UInt64Counter|UInt64Histogram|Int64UpDownCounter)\(([^;]*)\);`)

func TestFutilityCreationCallsCarryNoUnitArgument(t *testing.T) {
	source := codeLines(t, "../futility/otel/metrics.cc", "DescriptionFor")

	matches := cppCreateCall.FindAllSubmatch(source, -1)
	require.GreaterOrEqual(t, len(matches), 3,
		"found %d instrument-creation calls in metrics.cc; the recorder has at least a counter, "+
			"a histogram and an up-down counter, so the pattern here has gone stale", len(matches))

	for _, match := range matches {
		args := strings.Split(string(match[1]), ",")
		require.Len(t, args, 2,
			"a Create call in metrics.cc no longer has exactly (name, description) arguments: "+
				"Create...(%s). A third argument is the unit, which the collector folds into the "+
				"metric name (#1294); if the reshape is intentional, update this pin.", match[1])
		name := strings.TrimSpace(args[0])
		description := strings.TrimSpace(args[1])
		assert.Equal(t, "DescriptionFor("+name+")", description,
			"a Create call's second argument is not DescriptionFor(<name>): Create...(%s). If "+
				"the declaration was reshaped, update this pin — an unparseable call is one this "+
				"test stops checking.", match[1])
	}
}
