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
