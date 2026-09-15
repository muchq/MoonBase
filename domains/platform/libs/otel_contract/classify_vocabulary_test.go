package otel_contract

import (
	"os"
	"regexp"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The access-log vocabulary (#1150): stats' classify.go buckets user agents
// by three ordered marker lists and paths by an ordered list of probe
// families, and caddylog's classify.rs carries the same lists so a Rust
// reader of the same log lands every request in the same bucket. The two
// live in different languages; this is the one place that says they are
// the same lists, in the same order — order is the matching rule, since
// the first marker to match names the agent.

const (
	goClassify   = "../../apis/stats/classify.go"
	rustClassify = "../caddylog/src/classify.rs"
)

var quoted = regexp.MustCompile(`"([^"]*)"`)

func readSource(t *testing.T, path string) string {
	t.Helper()
	source, err := os.ReadFile(path)
	require.NoError(t, err, "cannot read %s — has the data dependency been dropped?", path)
	return string(source)
}

// The quoted strings inside the first match of pattern, in source order.
func quotedListFrom(t *testing.T, source, path string, pattern *regexp.Regexp) []string {
	t.Helper()
	match := pattern.FindStringSubmatch(source)
	require.NotNil(t, match, "no list matching %q in %s; if it was renamed, rename it here too", pattern, path)
	var values []string
	for _, m := range quoted.FindAllStringSubmatch(match[1], -1) {
		values = append(values, m[1])
	}
	require.NotEmpty(t, values, "parsed an empty list out of %s", path)
	return values
}

func TestAgentMarkerListsAgreeBetweenStatsAndCaddylog(t *testing.T) {
	goSource := readSource(t, goClassify)
	rustSource := readSource(t, rustClassify)

	for _, list := range []struct{ goName, rustName string }{
		{"aiScraperMarkers", "AI_SCRAPER_MARKERS"},
		{"namedBotMarkers", "NAMED_BOT_MARKERS"},
		{"botMarkers", "BOT_MARKERS"},
	} {
		// var aiScraperMarkers = []string{ ... }
		fromGo := quotedListFrom(t, goSource, goClassify,
			regexp.MustCompile(`var `+list.goName+` = \[\]string\{([^}]*)\}`))
		// pub const AI_SCRAPER_MARKERS: &[&str] = &[ ... ];
		fromRust := quotedListFrom(t, rustSource, rustClassify,
			regexp.MustCompile(`pub const `+list.rustName+`: &\[&str\] = &\[([^\]]*)\]`))
		assert.Equal(t, fromGo, fromRust,
			"%s and %s differ. A marker added on one side buckets the same agent differently "+
				"on the other; order matters too, since the first match names the agent.",
			list.goName, list.rustName)
	}
}

type probeFamily struct{ name, pattern string }

func TestProbeFamiliesAgreeBetweenStatsAndCaddylog(t *testing.T) {
	goSource := readSource(t, goClassify)
	rustSource := readSource(t, rustClassify)

	// ProbeTraversal = "traversal"
	names := map[string]string{}
	for _, m := range regexp.MustCompile(`(Probe\w+)\s*=\s*"([a-z]+)"`).FindAllStringSubmatch(goSource, -1) {
		names[m[1]] = m[2]
	}
	// {ProbeTraversal, regexp.MustCompile(`...`)},
	var fromGo []probeFamily
	for _, m := range regexp.MustCompile("\\{(Probe\\w+), regexp\\.MustCompile\\(`([^`]*)`\\)\\}").FindAllStringSubmatch(goSource, -1) {
		name, ok := names[m[1]]
		require.True(t, ok, "%s names a family with no string constant", m[1])
		fromGo = append(fromGo, probeFamily{name, m[2]})
	}
	require.NotEmpty(t, fromGo, "no probe family table found in %s", goClassify)

	// ("traversal", r"..."),
	table := regexp.MustCompile(`(?s)PROBE_FAMILIES: &\[\(&str, &str\)\] = &\[(.*?)\n\];`).FindStringSubmatch(rustSource)
	require.NotNil(t, table, "no PROBE_FAMILIES table found in %s", rustClassify)
	var fromRust []probeFamily
	for _, m := range regexp.MustCompile(`\(\s*"([a-z]+)",\s*r"([^"]*)",?\s*\)`).FindAllStringSubmatch(table[1], -1) {
		fromRust = append(fromRust, probeFamily{m[1], m[2]})
	}

	assert.Equal(t, fromGo, fromRust,
		"the probe families in %s and %s differ, by name, pattern or order. The first family to "+
			"match wins, so the order is part of the classification.", goClassify, rustClassify)
}
