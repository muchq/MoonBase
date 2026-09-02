package otel_contract

import (
	"os"
	"regexp"
	"sort"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The query-event vocabulary (#1465): one_d4 writes entry, source, outcome
// and cache from its QueryEvent constants, and the stats aggregator keys
// its rollup on the same words, collapsing any it does not know to
// "other". The two lists live in different languages; this is the one
// place that says they are the same list. A constant added on the Java
// side without its Go twin would silently become an "other" row.
func TestQueryEventVocabularyAgreesBetweenOneD4AndStats(t *testing.T) {
	java, err := os.ReadFile("../../../games/apis/one_d4/src/main/java/com/muchq/games/one_d4/api/QueryEvent.java")
	require.NoError(t, err)
	goSource, err := os.ReadFile("../../apis/stats/queries.go")
	require.NoError(t, err)

	for _, group := range []struct{ prefix, goMap string }{
		{"ENTRY_", "queryEntries"},
		{"SOURCE_", "querySources"},
		{"OUTCOME_", "queryOutcomes"},
		{"CACHE_", "queryCaches"},
	} {
		// static final String OUTCOME_OK = "ok";
		javaValues := regexp.MustCompile(`static final String `+group.prefix+`\w+ = "([^"]+)"`).
			FindAllStringSubmatch(string(java), -1)
		var fromJava []string
		for _, match := range javaValues {
			fromJava = append(fromJava, match[1])
		}
		// queryOutcomes = map[string]bool{"ok": true, "invalid": true, "failed": true}
		decl := regexp.MustCompile(group.goMap + `\s*=\s*map\[string\]bool\{([^}]*)\}`).
			FindStringSubmatch(string(goSource))
		require.NotNil(t, decl, "no %s map in queries.go", group.goMap)
		var fromGo []string
		for _, match := range regexp.MustCompile(`"([^"]+)":\s*true`).FindAllStringSubmatch(decl[1], -1) {
			fromGo = append(fromGo, match[1])
		}
		sort.Strings(fromJava)
		sort.Strings(fromGo)
		require.NotEmpty(t, fromJava, "no %s constants found in QueryEvent.java", group.prefix)
		assert.Equal(t, fromJava, fromGo,
			"QueryEvent.java's %s* values and queries.go's %s disagree; the stats rollup would "+
				"file the missing word under \"other\"", group.prefix, group.goMap)
	}
}
