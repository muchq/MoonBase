package otel_contract

import (
	"regexp"
	"testing"

	"github.com/stretchr/testify/assert"
)

// The gateway vocabulary: stats' route.go and caddylog's route.rs both
// name a request's backend from the Caddyfile's path matchers, and this
// is the one place that says their tables are the same lists in the same
// order. Order decides which matcher claims a path; no two matchers on a
// site overlap, which each language asserts for itself.
//
// caddylog pins its table equal to the Caddyfile, so agreement here is
// agreement with the gateway. testdata/routes.tsv is the behavioral half.

const goRoute = "../../apis/stats/route.go"
const rustRoute = "../caddylog/src/route.rs"

func TestSiteListsAgreeBetweenStatsAndCaddylog(t *testing.T) {
	// var sites = []string{ ... }
	fromGo := quotedListFrom(t, codeLines(t, goRoute, "sites"), goRoute,
		regexp.MustCompile(`var sites = \[\]string\{([^}]*)\}`))
	// pub const SITES: &[&str] = &[ ... ];
	fromRust := quotedListFrom(t, codeLines(t, rustRoute, "SITES"), rustRoute,
		regexp.MustCompile(`pub const SITES: &\[&str\] = &\[([^\]]*)\]`))
	assert.Equal(t, fromGo, fromRust,
		"the site lists differ. A host served by Caddy but missing on one side folds to "+
			"\"other\" there, so the same request is attributed to a different site.")
}

func TestRouteTablesAgreeBetweenStatsAndCaddylog(t *testing.T) {
	// var routes = []siteRoutes{ {"site", []string{ ... }}, ... }
	fromGo := quotedListFrom(t, codeLines(t, goRoute, "routes"), goRoute,
		regexp.MustCompile(`var routes = \[\]siteRoutes\{([\s\S]*?)\n\}`))
	// pub const ROUTES: &[(&str, &[&str])] = &[ ("site", &[ ... ]), ... ];
	fromRust := quotedListFrom(t, codeLines(t, rustRoute, "ROUTES"), rustRoute,
		regexp.MustCompile(`pub const ROUTES: &\[\(&str, &\[&str\]\)\] = &\[([\s\S]*?)\n\];`))
	assert.Equal(t, fromGo, fromRust,
		"the route tables differ, read as site then its matchers in order. A matcher on one "+
			"side only attributes the same request to a different backend, and a reordering "+
			"changes which matcher claims a path two of them could.")
}

func TestUnroutedSentinelsAgreeBetweenStatsAndCaddylog(t *testing.T) {
	for _, sentinel := range []struct {
		name string
		goRe *regexp.Regexp
		rsRe *regexp.Regexp
	}{
		{"site", regexp.MustCompile(`OtherSite\s+= "([a-z]+)"`),
			regexp.MustCompile(`pub const OTHER_SITE: &str = "([a-z]+)"`)},
		{"route", regexp.MustCompile(`OtherRoute\s+= "([a-z]+)"`),
			regexp.MustCompile(`pub const OTHER_ROUTE: &str = "([a-z]+)"`)},
	} {
		fromGo := sentinel.goRe.FindSubmatch(codeLines(t, goRoute, "Other"))
		fromRust := sentinel.rsRe.FindSubmatch(codeLines(t, rustRoute, "OTHER_"))
		if assert.NotNil(t, fromGo, "no %s sentinel in %s", sentinel.name, goRoute) &&
			assert.NotNil(t, fromRust, "no %s sentinel in %s", sentinel.name, rustRoute) {
			assert.Equal(t, string(fromGo[1]), string(fromRust[1]),
				"the unrouted %s token is spelled differently, so the two disagree about "+
					"which rows are the unattributed tail", sentinel.name)
		}
	}
}

// The caller vocabulary: one_d4 books its own queries as mcp, ui or api
// from its QueryEvent constants, and stats books every gateway request
// with the same three words. A word spelled differently on one side is a
// column that cannot be read beside the other.
func TestSourceVocabularyAgreesBetweenStatsAndOneD4(t *testing.T) {
	const goSource = "../../apis/stats/source.go"
	const javaEvent = "../../../games/apis/one_d4/src/main/java/com/muchq/games/one_d4/api/QueryEvent.java"

	fromJava := map[string]bool{}
	for _, match := range regexp.MustCompile(`static final String SOURCE_\w+ = "([^"]+)"`).
		FindAllStringSubmatch(string(codeLines(t, javaEvent, "SOURCE_")), -1) {
		fromJava[match[1]] = true
	}
	fromGo := map[string]bool{}
	for _, match := range regexp.MustCompile(`Source\w+\s+= "([^"]+)"`).
		FindAllStringSubmatch(string(codeLines(t, goSource, "Source")), -1) {
		fromGo[match[1]] = true
	}
	assert.NotEmpty(t, fromJava, "no SOURCE_ constants in %s", javaEvent)
	assert.Equal(t, fromJava, fromGo,
		"the caller vocabularies differ. stats books gateway arrivals and one_d4 books its own "+
			"work; the two only read beside each other while they spell the words the same.")

	// The words are shared; the evidence is not, and only one side has a
	// product token to read. one_d4 knows mcpserver by its User-Agent
	// because mcpserver calls it directly; at the gateway those calls
	// appear in no log at all, so a request carrying that token there is
	// by construction something else wearing it. stats keys on the route
	// instead, which is why it has no token for this pin to hold.
	assert.NotContains(t, string(codeLines(t, goSource, "SourceOf")), "mcpserver",
		"stats reads mcpserver's product token again. The gateway never sees mcpserver, "+
			"so the only caller that token can name there is one claiming it.")
}
