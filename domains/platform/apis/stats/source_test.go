package stats

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestSourceOfNamesTheCaller(t *testing.T) {
	for _, c := range []struct {
		name      string
		route     string
		origin    string
		userAgent string
		want      string
	}{
		{"the web app is known by the origin the browser attaches", "/1d4/v1/query", "https://1d4.net", "Mozilla/5.0", SourceUI},
		{"every origin caddy grants", "/games/v2/session", "https://muchq.com", "Mozilla/5.0", SourceUI},
		{"the dev origin too", "/games/v2/session", "http://localhost:5173", "Mozilla/5.0", SourceUI},
		{"an mcp client is known by the endpoint it reached", "/mcp", "", "node", SourceMCP},
		{"mcpserver by its user agent, wherever it lands", "/1d4/v1/query", "", "mcpserver/1.0", SourceMCP},
		{"and it wins over an origin, which it never sends", "/1d4/v1/query", "https://1d4.net", "mcpserver/1.0", SourceMCP},
		{"a direct caller has neither", "/1d4/v1/query", "", "curl/8.6.0", SourceAPI},
		{"an origin nobody granted is not the web app", "/1d4/v1/query", "https://evil.example.com", "Mozilla/5.0", SourceAPI},
		{"nor is a near miss", "/1d4/v1/query", "https://1d4.net.evil.example.com", "Mozilla/5.0", SourceAPI},
		{"a scanner on the mcp host never reached the endpoint", OtherRoute, "", "curl/8.6.0", SourceAPI},
		{"nothing at all", OtherRoute, "", "", SourceAPI},
	} {
		t.Run(c.name, func(t *testing.T) {
			assert.Equal(t, c.want, SourceOf(c.route, c.origin, c.userAgent))
		})
	}
}

// The header is caller-controlled, so the column is the bounded answer and
// never the header itself.
func TestUiOriginsIsAShortClosedList(t *testing.T) {
	assert.NotEmpty(t, uiOrigins)
	assert.Less(t, len(uiOrigins), 16, "uiOrigins has grown into a cardinality trap")
	for origin := range uiOrigins {
		assert.Equal(t, SourceUI, SourceOf(OtherRoute, origin, ""))
	}
}

func TestOriginReadsTheHeaderOrEmpty(t *testing.T) {
	line := &caddyLine{}
	line.Request.Headers = map[string][]string{"Origin": {"https://muchq.com", "https://ignored.example"}}
	assert.Equal(t, "https://muchq.com", line.origin())
	assert.Equal(t, "", (&caddyLine{}).origin())
}

// The MCP evidence is a route, so it has to be one the table claims. A
// gateway rename would otherwise empty the bucket in silence.
func TestMcpRouteIsTheOneTheTableClaims(t *testing.T) {
	assert.Equal(t, mcpRoute, RouteOf("mcp.1d4.net", mcpRoute))
}
