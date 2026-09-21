package stats

import "testing"

func TestServiceOfNamesTheBackendBehindTheRoute(t *testing.T) {
	for _, c := range []struct{ name, site, route, want string }{
		{"a matcher's own upstream", "api.muchq.com", "/1d4/v1/query", "one_d4"},
		{"several routes reach one service", "api.muchq.com", "/1d4/v1/index/*", "one_d4"},
		{"a prefix matcher too", "api.muchq.com", "/deja/v1/*", "deja"},
		{"the same path on another site is its own entry", "gpt.muchq.com", "/microgpt/v1/chat", "microgpt-serve"},
		// The whole point of the table: route says "other" for every
		// Forgejo page, because git.muchq.com has no path matchers at all.
		{"a route-blind site serves everything from one backend", "git.muchq.com", OtherRoute, "forgejo"},
		{"and names it for any route", "git.muchq.com", "/muchq/moonbase", "forgejo"},
		// Where a site does route by path, an unclaimed path reached no
		// backend — the gateway answered it itself.
		{"an unclaimed path on a routed site reached nothing", "api.muchq.com", OtherRoute, OtherService},
		{"a route no matcher on that site spells", "api.muchq.com", "/r/*", OtherService},
		{"a host the gateway does not serve", OtherSite, OtherRoute, OtherService},
	} {
		t.Run(c.name, func(t *testing.T) {
			if got := ServiceOf(c.site, c.route); got != c.want {
				t.Errorf("ServiceOf(%q, %q) = %q, want %q", c.site, c.route, got, c.want)
			}
		})
	}
}

// A route is named for the backend it is proxied to, which is not always
// the thing that answered: Caddy answers preflights and refusals itself,
// above the handle that would have proxied them. Those rows still carry
// the backend's name, and this says so rather than leaving a reader to
// discover it from a suspiciously busy service.
func TestServiceNamesWhereTheRouteGoesNotWhoAnswered(t *testing.T) {
	// An OPTIONS preflight on this path is a 204 from the gateway; the row
	// reads as portrait because the route is portrait's.
	if got := ServiceOf("api.muchq.com", "/portrait/v1/trace"); got != "portrait" {
		t.Errorf("a preflight on portrait's route = %q, want portrait", got)
	}
	// And a crawler refused above the bare handle still reads as forgejo,
	// where it lands in that service's errors.
	if got := ServiceOf("git.muchq.com", OtherRoute); got != "forgejo" {
		t.Errorf("a refusal on git.muchq.com = %q, want forgejo", got)
	}
}

// One entry per site. ServiceOf returns on the first entry whose site
// matches, where RouteOf falls through to the next, so a duplicate would
// make every route in the second entry read as "other" — and the text
// parser in deploy_config_test merges duplicates into one map, so the
// gateway pin would stay green while it happened.
func TestEachSiteHasOneServiceEntry(t *testing.T) {
	seen := map[string]bool{}
	for _, entry := range services {
		if seen[entry.Site] {
			t.Errorf("%s has more than one entry; every route in the later one reads as %q",
				entry.Site, OtherService)
		}
		seen[entry.Site] = true
	}
}

// Every route the router can produce has a service, or the column is a
// vocabulary with holes: a request would carry a route naming a backend
// and a service saying none answered.
func TestEveryRouteInTheTableHasAService(t *testing.T) {
	for _, site := range routes {
		for _, route := range site.Routes {
			if got := ServiceOf(site.Site, route); got == OtherService {
				t.Errorf("%s on %s has a matcher but no service", route, site.Site)
			}
		}
	}
}

// And no service entry names a site the gateway does not serve, which
// would be a row ServiceOf can never return.
func TestEveryServiceEntryNamesAKnownSite(t *testing.T) {
	known := map[string]bool{}
	for _, site := range sites {
		known[site] = true
	}
	for _, entry := range services {
		if !known[entry.Site] {
			t.Errorf("services names %q, which is not one of the Caddyfile's sites", entry.Site)
		}
	}
}
