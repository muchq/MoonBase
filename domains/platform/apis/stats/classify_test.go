package stats

import (
	"os"
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The corpus under caddylog/testdata is the behavioral pin shared with the
// Rust classifier (#1150): tab-separated rows, `#` comments, blank lines
// ignored, read from the package dir where rules_go runs the test.
func corpusRows(t *testing.T, path string, columns int) [][]string {
	t.Helper()
	source, err := os.ReadFile(path)
	require.NoError(t, err, "cannot read %s — has the data dependency been dropped?", path)
	var rows [][]string
	for _, line := range strings.Split(string(source), "\n") {
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		row := strings.Split(line, "\t")
		require.Len(t, row, columns, "malformed corpus row %q", line)
		rows = append(rows, row)
	}
	require.Greater(t, len(rows), 10, "corpus too small to mean anything")
	return rows
}

func TestAgentsCorpusLandsEveryLineWhereCaddylogDoes(t *testing.T) {
	classes := map[string]bool{}
	for _, row := range corpusRows(t, "../../libs/caddylog/testdata/agents.tsv", 3) {
		ua, wantClass, wantName := row[0], row[1], row[2]
		class, name := AgentOf(ua)
		if class != wantClass || name != wantName {
			t.Errorf("AgentOf(%q) = (%s, %q), want (%s, %q)", ua, class, name, wantClass, wantName)
		}
		classes[class] = true
	}
	require.Len(t, classes, 4, "the corpus reaches every class")
}

func TestAgentNamesAreBoundedPerClass(t *testing.T) {
	// Every marker names itself, so the agent column's vocabulary for the
	// two marker classes is exactly the lists and cannot drift from them —
	// and markerNames knows every one of them.
	for _, marker := range aiScraperMarkers {
		ua := "Mozilla/5.0 (compatible; " + strings.ToUpper(marker) + "/1.0)"
		if class, name := AgentOf(ua); class != AgentAIScraper || name != marker {
			t.Errorf("AgentOf(%q) = (%s, %q), want (%s, %q)", ua, class, name, AgentAIScraper, marker)
		}
		if !markerNames[marker] {
			t.Errorf("markerNames lacks %q", marker)
		}
	}
	for _, marker := range namedBotMarkers {
		ua := "Mozilla/5.0 (compatible; " + strings.ToUpper(marker) + "/1.0)"
		if class, name := AgentOf(ua); class != AgentBot || name != marker {
			t.Errorf("AgentOf(%q) = (%s, %q), want (%s, %q)", ua, class, name, AgentBot, marker)
		}
		if !markerNames[marker] {
			t.Errorf("markerNames lacks %q", marker)
		}
	}
	// A generic marker that a named one already covers is unreachable;
	// keeping the lists disjoint is what makes the named list the vocabulary.
	for _, marker := range botMarkers {
		if markerNames[marker] {
			t.Errorf("botMarkers repeats %q, which namedBotMarkers matches first", marker)
		}
	}
}

// The bare "bot" marker has no word boundary, so a phone brand ending in
// it reads as a bot. Known and kept: a boundary rule would also lose
// "Googlebot"-shaped names, and the AI list is consulted first regardless.
func TestBotSubstringHasNoWordBoundaryOnPurpose(t *testing.T) {
	if class, name := AgentOf("Mozilla/5.0 (Linux; Android 10; CUBOT X30) AppleWebKit/537.36 Chrome/120 Mobile Safari/537.36"); class != AgentBot || name != "bot" {
		t.Errorf("CUBOT = (%s, %q); if this changed on purpose, update the comment above", class, name)
	}
}

func TestProbesCorpusLandsEveryLineWhereCaddylogDoes(t *testing.T) {
	reached := map[string]bool{}
	for _, row := range corpusRows(t, "../../libs/caddylog/testdata/probes.tsv", 2) {
		uri, want := row[0], row[1]
		got := ProbeOf(uri)
		if got != want {
			t.Errorf("ProbeOf(%q) = %q, want %q", uri, got, want)
		}
		if got != "" {
			reached[got] = true
		}
	}
	// Every family is reached by a row, so a pattern that stops matching
	// fails here rather than silently never firing again.
	for _, family := range probeFamilies {
		assert.True(t, reached[family.name], "no corpus row reaches the %q family", family.name)
	}
}

func TestSlugExtractionIsBoundedAndRouteScoped(t *testing.T) {
	cases := []struct {
		host, method, uri string
		want              string
	}{
		{"i.iili.uk", "GET", "/r/abc123", "abc123"},
		{"i.iili.uk", "HEAD", "/r/abc123?utm=x", "abc123"},
		{"api.muchq.com", "GET", "/iili/v1/r/xyz", "xyz"},
		// POSTs are not redirect lookups; deep paths and oversized slugs
		// are scanner shapes, not slugs.
		{"i.iili.uk", "POST", "/r/abc123", ""},
		{"i.iili.uk", "GET", "/r/a/b", ""},
		{"i.iili.uk", "GET", "/r/", ""},
		{"i.iili.uk", "GET", "/r/" + strings.Repeat("a", 100), ""},
		{"api.muchq.com", "GET", "/portrait/v1/trace", ""},
		{"git.muchq.com", "GET", "/r/abc", ""},
		// Only api.muchq.com routes /iili/v1/r/ to iili, and only for GET;
		// anywhere else Caddy answers the path itself, so nothing was followed.
		{"gpt.muchq.com", "GET", "/iili/v1/r/anything", ""},
		{"api.muchq.com", "HEAD", "/iili/v1/r/xyz", ""},
	}
	for _, c := range cases {
		if got := SlugOf(c.host, c.method, c.uri); got != c.want {
			t.Errorf("SlugOf(%q, %s, %q) = %q, want %q", c.host, c.method, c.uri, got, c.want)
		}
	}
}
