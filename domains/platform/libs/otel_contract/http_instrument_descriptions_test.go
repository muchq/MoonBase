package otel_contract

import (
	"os"
	"regexp"
	"strings"
	"testing"
	"unicode"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The five instruments every HTTP service reports, whatever it is written in.
// A rail that stops declaring one of these has not gone quiet — it has started
// reporting an instrument with no description, which is its own conflict.
var sharedHttpInstruments = []string{
	"http_server_requests",
	"http_server_requests_success",
	"http_server_requests_failure",
	"http_server_requests_active_gauge",
	"http_server_request_duration_microseconds",
}

// Where each rail declares its http_server_* descriptions, and the patterns
// that lift (instrument name, description) pairs out of it. Each rail needs
// more than one pattern because none of them writes all five the same way: the
// counters go through a shared helper and the gauge and histogram do not.
//
// Every pattern captures the name first and the description second. Paths are
// relative to this package, which is where rules_go runs the test from.
var descriptionRails = []struct {
	name     string
	path     string
	patterns []*regexp.Regexp
}{
	{
		name: "futility/otel (C++)",
		path: "../futility/otel/http_instrument_descriptions.h",
		patterns: []*regexp.Regexp{
			// {"http_server_requests", "HTTP requests received"},
			regexp.MustCompile(`\{"(http_server_[a-z_]*)",\s*"([^"]*)"\}`),
		},
	},
	{
		name: "yodel (Java)",
		path: "../yodel/src/main/java/com/muchq/platform/yodel/OtlpJsonEncoder.java",
		patterns: []*regexp.Regexp{
			// sum("http_server_requests", "HTTP requests received", ...)
			regexp.MustCompile(`"(http_server_[a-z_]*)",\s*\n\s*"([^"]*)"`),
			// metric.put("name", "..."); metric.put("description", "...");
			regexp.MustCompile(`"name",\s*"(http_server_[a-z_]*)"\);\s*\n\s*metric\.put\("description",\s*"([^"]*)"\)`),
		},
	},
	{
		name: "server_pal (Rust)",
		path: "../server_pal/src/lib.rs",
		patterns: []*regexp.Regexp{
			// Every instrument is a builder chain in HttpInstruments::new:
			// .u64_counter("http_server_requests")
			//     .with_description("HTTP requests received")
			regexp.MustCompile(`\("(http_server_[a-z_]*)"\)\s*\n\s*\.with_description\("([^"]*)"\)`),
		},
	},
}

// descriptionsFrom returns the instrument-name-to-description map a rail
// declares, restricted to the shared instruments. Names outside that set are
// ignored: a service is free to invent instruments, and only the shared ones
// are reported by more than one rail and so only they can conflict.
func descriptionsFrom(t *testing.T, path string, patterns []*regexp.Regexp) map[string]string {
	t.Helper()
	source, err := os.ReadFile(path)
	require.NoError(t, err, "cannot read %s — has the data dependency been dropped?", path)
	source = withoutCommentLines(source)

	shared := map[string]bool{}
	for _, name := range sharedHttpInstruments {
		shared[name] = true
	}

	found := map[string]string{}
	for _, pattern := range patterns {
		for _, match := range pattern.FindAllSubmatch(source, -1) {
			name, description := string(match[1]), string(match[2])
			if !shared[name] {
				continue
			}
			if existing, seen := found[name]; seen {
				require.Equal(t, existing, description,
					"%s declares %s twice with different descriptions", path, name)
				continue
			}
			found[name] = description
		}
	}

	// Guards the parse itself. Every pattern here is matching source text, so a
	// refactor that reformats a declaration breaks the match rather than the
	// build, and a rail this test silently stopped reading is a rail it stopped
	// checking — the failure mode the bucket pin was written to avoid too.
	for _, name := range sharedHttpInstruments {
		require.Contains(t, found, name,
			"no description found for %s in %s. If the declaration was reshaped, update the "+
				"pattern here: a rail this test cannot parse is one it no longer pins.",
			name, path)
	}
	return found
}

// withoutCommentLines blanks whole-line comments in C++, Java and Rust source.
//
// Every rail's declaration file explains this contract in prose, and the prose
// quotes the strings — the C++ header reproduces a table entry in its own doc
// comment. Matching over comments lets a rail satisfy the pin with an example
// while declaring nothing: delete the real entry, leave the sentence that
// describes it, and both tests below still pass while the service exports an
// empty description. Lines are blanked rather than removed so that patterns
// spanning a newline cannot bridge across the gap a deleted line would leave.
func withoutCommentLines(source []byte) []byte {
	lines := strings.Split(string(source), "\n")
	for i, line := range lines {
		trimmed := strings.TrimSpace(line)
		for _, marker := range []string{"//", "/*", "*/", "*"} {
			if strings.HasPrefix(trimmed, marker) {
				lines[i] = ""
				break
			}
		}
	}
	return []byte(strings.Join(lines, "\n"))
}

// The three emitters describe the shared instruments identically.
//
// All of them report into one collector, which merges series by instrument
// name across services. Its Prometheus exporter keeps the first description it
// sees for a name and logs "Instrument description conflict, using existing"
// for every later one that disagrees — once per export interval, for as long as
// both services run. The series still export, so the only symptom is a log the
// collector floods with, which is why this drifted unnoticed until it was
// three-way.
//
// An empty description conflicts with a non-empty one exactly as loudly, so
// "this rail just won't say" is not a way out; it is how futility got here.
func TestHttpInstrumentDescriptionsMatchAcrossAllThreeEmitters(t *testing.T) {
	byRail := map[string]map[string]string{}
	for _, rail := range descriptionRails {
		byRail[rail.name] = descriptionsFrom(t, rail.path, rail.patterns)
	}
	require.Len(t, byRail, len(descriptionRails), "two rails share a name")

	reference := descriptionRails[0]
	want := byRail[reference.name]
	for _, rail := range descriptionRails[1:] {
		assert.Equal(t, want, byRail[rail.name],
			"%s and %s describe the shared HTTP instruments differently. They have to move "+
				"together — the collector merges these series by name across all three, and keeps "+
				"only the first description it sees.", reference.name, rail.name)
	}
}

// And that the agreed descriptions are ASCII.
//
// The drift this test was written for was a single en-dash in server_pal's
// "(2xx–3xx)" against yodel's "(2xx-3xx)". Nothing renders it: the two strings
// are indistinguishable in a diff, in a review, and on a dashboard, and differ
// only to the byte comparison the collector actually makes. Equality above
// would be satisfied by all three rails adopting the en-dash together, so pin
// the property that makes the mismatch visible in the first place.
func TestHttpInstrumentDescriptionsAreAscii(t *testing.T) {
	for _, rail := range descriptionRails {
		t.Run(rail.name, func(t *testing.T) {
			for name, description := range descriptionsFrom(t, rail.path, rail.patterns) {
				assert.NotEmpty(t, description,
					"%s describes %s with an empty string, which conflicts with every rail "+
						"that describes it at all", rail.name, name)

				for _, r := range description {
					require.LessOrEqual(t, r, unicode.MaxASCII,
						"%s describes %s with the non-ASCII character %q. It reads the same as "+
							"its ASCII twin and exports as a different string: %q",
						rail.name, name, r, description)
				}
			}
		})
	}
}
