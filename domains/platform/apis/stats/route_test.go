package stats

import (
	"strings"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

// The behavioral half of the cross-language pin: the corpus caddylog's
// route.rs replays, so a log line names the same backend in either
// language. The vocabulary half is in otel_contract.
func TestRoutesCorpusLandsEveryLineWhereCaddylogDoes(t *testing.T) {
	seen := map[string]bool{}
	for _, row := range corpusRows(t, "../../libs/caddylog/testdata/routes.tsv", 3) {
		host, uri, want := row[0], row[1], row[2]
		assert.Equal(t, want, RouteOf(host, uri), "host %q target %q", host, uri)
		seen[SiteOf(host)+"\t"+want] = true
	}
	require.True(t, seen[OtherSite+"\t"+OtherRoute], "corpus never reaches the unrouted token")
	// Every matcher has a row on its own site, so an entry missing from the
	// table fails here and not only in the text-reading vocabulary pin. The
	// site is part of the key because two sites share a spelling: covering
	// /microgpt/v1/chat on api.muchq.com says nothing about gpt.muchq.com.
	for _, site := range routes {
		for _, route := range site.Routes {
			assert.True(t, seen[site.Site+"\t"+route], "%s on %s has no corpus row", route, site.Site)
		}
	}
}

func TestSiteOfFoldsTheHostAndBoundsIt(t *testing.T) {
	assert.Equal(t, "api.muchq.com", SiteOf("API.muchq.com:443"))
	assert.Equal(t, OtherSite, SiteOf("evil.example.com"))
	assert.Equal(t, OtherSite, SiteOf(""))
	// A trailing dot names the same vhost, and Caddy's host matcher reads
	// it as one; unfolded it would file real traffic under "other".
	assert.Equal(t, "api.muchq.com", SiteOf("api.muchq.com."))
	assert.Equal(t, "api.muchq.com", SiteOf("API.MUCHQ.COM.:443"))
}

// No entry is shadowed by an earlier prefix on its own site. First match
// wins over a literal table, so a shadowed entry would be silently dead.
// Drift from the Caddyfile is caddylog's pin, not this one.
func TestEveryRouteIsReachableByItsOwnMatcher(t *testing.T) {
	for _, site := range routes {
		for _, route := range site.Routes {
			target := route
			if prefix, ok := strings.CutSuffix(route, "*"); ok {
				target = prefix + "reached"
			}
			assert.Equal(t, route, RouteOf(site.Site, target),
				"%s on %s is claimed by another matcher", route, site.Site)
		}
	}
}
