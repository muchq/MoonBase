package otel_contract

import (
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
// the first marker to match names the agent. The behavioral half of the
// pin is the corpus under caddylog/testdata, replayed by both suites.

const (
	goClassify   = "../../apis/stats/classify.go"
	goAggregate  = "../../apis/stats/aggregate.go"
	rustClassify = "../caddylog/src/classify.rs"
)

var quoted = regexp.MustCompile(`"([^"]*)"`)

// The quoted strings inside the first match of pattern, in source order.
func quotedListFrom(t *testing.T, source []byte, path string, pattern *regexp.Regexp) []string {
	t.Helper()
	match := pattern.FindSubmatch(source)
	require.NotNil(t, match, "no list matching %q in %s; if it was renamed, rename it here too", pattern, path)
	var values []string
	for _, m := range quoted.FindAllSubmatch(match[1], -1) {
		values = append(values, string(m[1]))
	}
	require.NotEmpty(t, values, "parsed an empty list out of %s", path)
	return values
}

func TestAgentMarkerListsAgreeBetweenStatsAndCaddylog(t *testing.T) {
	goSource := codeLines(t, goClassify, "aiScraperMarkers")
	rustSource := codeLines(t, rustClassify, "AI_SCRAPER_MARKERS")

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

// The four class spellings are the stats tables' column values, so a
// rename on one side is a row the other side never keys.
func TestAgentClassSpellingsAgreeBetweenStatsAndCaddylog(t *testing.T) {
	goSource := codeLines(t, goClassify, "AgentAIScraper")
	rustSource := codeLines(t, rustClassify, "AgentClass::AiScraper")

	// AgentAIScraper = "ai_scraper"
	var fromGo []string
	for _, m := range regexp.MustCompile(`\bAgent\w+\s*=\s*"([a-z_]+)"`).FindAllSubmatch(goSource, -1) {
		fromGo = append(fromGo, string(m[1]))
	}
	// AgentClass::AiScraper => "ai_scraper",
	var fromRust []string
	for _, m := range regexp.MustCompile(`AgentClass::\w+ => "([a-z_]+)"`).FindAllSubmatch(rustSource, -1) {
		fromRust = append(fromRust, string(m[1]))
	}
	require.Len(t, fromGo, 4, "expected four class constants in %s", goClassify)
	assert.Equal(t, fromGo, fromRust, "the agent classes in %s and %s differ", goClassify, rustClassify)
}

// The nine verbs that pass through method bounding; the tenth value,
// CUSTOM, is asserted per rail.
func TestKnownMethodsAgreeBetweenStatsAndCaddylog(t *testing.T) {
	goSource := codeLines(t, goAggregate, "knownMethods")
	rustSource := codeLines(t, rustClassify, "KNOWN")

	fromGo := quotedListFrom(t, goSource, goAggregate,
		regexp.MustCompile(`var knownMethods = map\[string\]bool\{([^}]*)\}`))
	fromRust := quotedListFrom(t, rustSource, rustClassify,
		regexp.MustCompile(`const KNOWN: \[&str; \d+\] =\s*\[([^\]]*)\]`))
	assert.Equal(t, fromGo, fromRust, "the bounded method lists in %s and %s differ", goAggregate, rustClassify)
}

type probeFamily struct{ name, pattern string }

func TestProbeFamiliesAgreeBetweenStatsAndCaddylog(t *testing.T) {
	goSource := codeLines(t, goClassify, "probeFamilies")
	rustSource := codeLines(t, rustClassify, "PROBE_FAMILIES")

	// ProbeTraversal = "traversal"
	names := map[string]string{}
	for _, m := range regexp.MustCompile(`(Probe\w+)\s*=\s*"([a-z]+)"`).FindAllSubmatch(goSource, -1) {
		names[string(m[1])] = string(m[2])
	}
	// {ProbeTraversal, regexp.MustCompile(`...`)},
	var fromGo []probeFamily
	for _, m := range regexp.MustCompile("\\{(Probe\\w+), regexp\\.MustCompile\\(`([^`]*)`\\)\\}").FindAllSubmatch(goSource, -1) {
		name, ok := names[string(m[1])]
		require.True(t, ok, "%s names a family with no string constant", m[1])
		fromGo = append(fromGo, probeFamily{name, string(m[2])})
	}
	require.NotEmpty(t, fromGo, "no probe family table found in %s", goClassify)

	// ("traversal", r"..."),
	table := regexp.MustCompile(`(?s)PROBE_FAMILIES: &\[\(&str, &str\)\] = &\[(.*?)\n\];`).FindSubmatch(rustSource)
	require.NotNil(t, table, "no PROBE_FAMILIES table found in %s", rustClassify)
	var fromRust []probeFamily
	for _, m := range regexp.MustCompile(`\(\s*"([a-z]+)",\s*r"([^"]*)",?\s*\)`).FindAllSubmatch(table[1], -1) {
		fromRust = append(fromRust, probeFamily{string(m[1]), string(m[2])})
	}

	assert.Equal(t, fromGo, fromRust,
		"the probe families in %s and %s differ, by name, pattern or order. The first family to "+
			"match wins, so the order is part of the classification.", goClassify, rustClassify)
}
