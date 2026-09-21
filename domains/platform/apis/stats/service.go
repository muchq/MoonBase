package stats

// The service behind a route: the container the Caddyfile proxies it to.
//
// Route names the matcher that claimed the path, which is not the same
// question. Two routes reach one service (/1d4/v1/query and /1d4/v1/index
// are both one_d4), and a site with no path matchers at all reaches one
// service from every path — git.muchq.com proxies the whole vhost to
// forgejo from a bare handle, so every Forgejo page is OtherRoute and
// grouping by route pools all of it with the paths nothing served.
//
// Derived at read time rather than stored: the mapping is the Caddyfile's
// to change, and a service renamed or re-pointed should not cost a
// re-aggregation of every object in S3. The price is that a re-point
// rewrites the past rather than dividing it — move /imagine/v1/blur off
// posterize and every row of the last year reads as the new container,
// with nothing in the response saying so. Rare enough to be worth it,
// wrong enough to say out loud. deploy_config_test pins the table to the
// gateway, both directions.
type siteServices struct {
	Site string
	// The service a site proxies every path to, for a site whose handle
	// carries no path matcher. Empty where the site routes by path: there
	// an unclaimed path reached no backend at all.
	Default string
	ByRoute map[string]string
}

var services = []siteServices{
	{Site: "api.1d4.net", ByRoute: map[string]string{
		"/health":            "one_d4",
		"/stats/v1/one_d4/*": "stats",
		"/v1/index":          "one_d4",
		"/v1/index/*":        "one_d4",
		"/v1/query":          "one_d4",
	}},
	{Site: "api.muchq.com", ByRoute: map[string]string{
		"/1d4/v1/health":        "one_d4",
		"/1d4/v1/index":         "one_d4",
		"/1d4/v1/index/*":       "one_d4",
		"/1d4/v1/query":         "one_d4",
		"/deja/v1/*":            "deja",
		"/games/v2/play":        "games_hub",
		"/games/v2/session":     "games_hub",
		"/iili/v1/r/*":          "iili",
		"/iili/v1/shorten":      "iili",
		"/imagine/v1/blur":      "posterize",
		"/imagine/v1/edges":     "posterize",
		"/metrics/v1/*":         "prom_proxy",
		"/microgpt/v1/chat":     "microgpt-serve",
		"/microgpt/v1/generate": "microgpt-serve",
		"/mithril/v1/wordchain": "mithril",
		"/portrait/v1/trace":    "portrait",
		"/stats/v1/*":           "stats",
		"/v2/analyze":           "one_d4_v2",
	}},
	{Site: "git.muchq.com", Default: "forgejo"},
	{Site: "gpt.muchq.com", ByRoute: map[string]string{
		"/microgpt/v1/chat":     "microgpt-serve",
		"/microgpt/v1/generate": "microgpt-serve",
	}},
	{Site: "i.iili.uk", ByRoute: map[string]string{"/r/*": "iili"}},
	{Site: "mcp.1d4.net", ByRoute: map[string]string{"/mcp": "mcpserver"}},
}

// The service of a request no backend answered: a path the gateway itself
// refused or 404'd, or a host it does not serve. One token, like the site
// and route it comes from.
const OtherService = "other"

// ServiceOf names the container a request's route is proxied to, from the
// site it addressed and the route that claimed it, or OtherService.
//
// Proxied to, not answered by. Caddy answers some requests itself above
// the handle that proxies: a CORS preflight is a 204 from the gateway, and
// refuse_bots 403s a scraper, both on paths that belong to a backend. Those
// rows carry that backend's name, and its errors carry those refusals —
// worst on git.muchq.com, where the crawler refusals of #1447 all read as
// forgejo. The probe rollup documents the same shape for the same reason.
func ServiceOf(site, route string) string {
	for _, candidate := range services {
		if candidate.Site != site {
			continue
		}
		if service, ok := candidate.ByRoute[route]; ok {
			return service
		}
		// A site that routes by path and claimed none of them served
		// nothing; a site that routes by nothing served this too.
		if candidate.Default != "" {
			return candidate.Default
		}
		return OtherService
	}
	return OtherService
}
