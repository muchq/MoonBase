package stats

import "strings"

// Which backend served a request, named by the Caddyfile matcher that
// claimed it. Eleven services sit behind api.muchq.com, so host names
// none of them.
//
// caddylog's route.rs answers this in Rust for deja and has to agree
// token for token: otel_contract pins the two tables equal and
// testdata/routes.tsv pins the two answers. The Rust table is pinned to
// the Caddyfile, so agreement with it is agreement with the gateway.

// Every site block in deploy/consolidated/Caddyfile, folded: the bounded
// host vocabulary, since a request's host is whatever the client sent.
var sites = []string{
	"api.1d4.net",
	"api.muchq.com",
	"consolidated.cmptr.info",
	"git.muchq.com",
	"gpt.muchq.com",
	"i.iili.uk",
	"mcp.1d4.net",
}

type siteRoutes struct {
	Site   string
	Routes []string
}

// Every path matcher in the Caddyfile, by site, spelled as it spells them.
// A trailing /* is a prefix matcher, anything else is exact; no two entries
// on a site overlap. Where the Caddyfile splits a path out of a prefix to
// route it by method — POST /deja/v1/next under GET /deja/v1/* — the table
// keeps the prefix that claims the path, since method is a column already.
var routes = []siteRoutes{
	{"api.1d4.net", []string{
		"/health",
		"/stats/v1/one_d4/*",
		"/v1/index",
		"/v1/index/*",
		"/v1/query",
	}},
	{"api.muchq.com", []string{
		"/1d4/v1/health",
		"/1d4/v1/index",
		"/1d4/v1/index/*",
		"/1d4/v1/query",
		"/deja/v1/*",
		"/games/v2/play",
		"/games/v2/session",
		"/iili/v1/r/*",
		"/iili/v1/shorten",
		"/imagine/v1/blur",
		"/imagine/v1/edges",
		"/metrics/v1/*",
		"/microgpt/v1/chat",
		"/microgpt/v1/generate",
		"/mithril/v1/wordchain",
		"/portrait/v1/trace",
		"/stats/v1/*",
		"/v2/analyze",
	}},
	{"gpt.muchq.com", []string{
		"/microgpt/v1/chat",
		"/microgpt/v1/generate",
	}},
	{"i.iili.uk", []string{"/r/*"}},
	{"mcp.1d4.net", []string{"/mcp"}},
}

// The host of a request nothing in the Caddyfile serves, and the route of
// one no matcher claims: one token each, so the unrouted tail cannot mint a
// row per path a scanner invents.
const (
	OtherSite  = "other"
	OtherRoute = "other"
)

// The MCP endpoint, which is how a request reaching it is known at the
// gateway. The only matcher on mcp.1d4.net, so the route alone names it.
const mcpRoute = "/mcp"

// SiteOf names the Caddyfile site a request's host addresses, port and case
// dropped, or OtherSite.
func SiteOf(host string) string {
	if colon := strings.IndexByte(host, ':'); colon >= 0 {
		host = host[:colon]
	}
	// The port, the case and the FQDN's trailing dot are the client's to
	// vary; Caddy's host matcher ignores all three, so the same vhost is
	// reached under any of them and the column has to fold them all.
	host = strings.TrimSuffix(strings.ToLower(host), ".")
	for _, site := range sites {
		if site == host {
			return site
		}
	}
	return OtherSite
}

// RouteOf names the Caddyfile matcher that claims uri on host, or
// OtherRoute. The path is matched the way Caddy's path matcher matches it:
// percent escapes decoded, dot segments and doubled slashes cleaned, a
// trailing slash kept, then compared case-insensitively.
func RouteOf(host, uri string) string {
	site := SiteOf(host)
	target, ok := matchTarget(uri)
	if !ok {
		return OtherRoute
	}
	path := strings.ToLower(target)
	for _, candidate := range routes {
		if candidate.Site != site {
			continue
		}
		for _, route := range candidate.Routes {
			if prefix, isPrefix := strings.CutSuffix(route, "*"); isPrefix {
				if strings.HasPrefix(path, prefix) {
					return route
				}
			} else if path == route {
				return route
			}
		}
	}
	return OtherRoute
}

// What the client asked for: the request target's path with its percent
// escapes decoded, case left alone. Probe classification reads this rather
// than the cleaned path below, because a scanner's dot segments are the
// evidence — cleaning them away is precisely what destroys the traversal
// family's signal — while an escape is only a spelling, so /%2Eenv has to
// land in the same family as /.env.
func decodedTarget(uri string) (string, bool) {
	target, ok := originPath(uri)
	if !ok {
		return "", false
	}
	return percentDecode(target), true
}

// The path Caddy's matcher compares against: decodedTarget with its dot
// segments and doubled slashes cleaned. Route and slug both read this, so
// a row cannot name a route the request never reached, or miss a slug the
// backend was handed.
func matchTarget(uri string) (string, bool) {
	target, ok := decodedTarget(uri)
	if !ok {
		return "", false
	}
	return clean(target), true
}

// The path of a request target as Caddy's matcher sees it, query string
// dropped: origin-form (/a/b) as is, absolute-form (https://host/a/b) from
// the first slash after the authority, and nothing for the asterisk and
// authority forms or an absolute-form with no path, which carry no path for
// a matcher to claim.
func originPath(target string) (string, bool) {
	if q := strings.IndexByte(target, '?'); q >= 0 {
		target = target[:q]
	}
	if strings.HasPrefix(target, "/") {
		return target, true
	}
	_, afterScheme, found := strings.Cut(target, "://")
	if !found {
		return "", false
	}
	slash := strings.IndexByte(afterScheme, '/')
	if slash < 0 {
		return "", false
	}
	return afterScheme[slash:], true
}

// %XX to the byte it names, bytewise: a bad escape stays literal and does
// not stop the good ones decoding. url.PathUnescape fails the whole
// target instead, which is not how Caddy reads it.
func percentDecode(raw string) string {
	var out []byte
	for i := 0; i < len(raw); {
		if raw[i] == '%' && i+2 < len(raw) {
			if b, ok := hexByte(raw[i+1], raw[i+2]); ok {
				out = append(out, b)
				i += 3
				continue
			}
		}
		out = append(out, raw[i])
		i++
	}
	return string(out)
}

func hexByte(hi, lo byte) (byte, bool) {
	h, okHi := hexNibble(hi)
	l, okLo := hexNibble(lo)
	return h<<4 | l, okHi && okLo
}

func hexNibble(c byte) (byte, bool) {
	switch {
	case c >= '0' && c <= '9':
		return c - '0', true
	case c >= 'a' && c <= 'f':
		return c - 'a' + 10, true
	case c >= 'A' && c <= 'F':
		return c - 'A' + 10, true
	}
	return 0, false
}

// Caddy's CleanPath: . segments drop, .. pops, doubled slashes collapse,
// the result is rooted, and a trailing slash survives. path.Clean drops
// that slash, and matchers here turn on it.
func clean(path string) string {
	var segments []string
	for _, segment := range strings.Split(path, "/") {
		switch segment {
		case "", ".":
		case "..":
			if len(segments) > 0 {
				segments = segments[:len(segments)-1]
			}
		default:
			segments = append(segments, segment)
		}
	}
	var cleaned strings.Builder
	for _, segment := range segments {
		cleaned.WriteByte('/')
		cleaned.WriteString(segment)
	}
	if cleaned.Len() == 0 || strings.HasSuffix(path, "/") {
		cleaned.WriteByte('/')
	}
	return cleaned.String()
}
