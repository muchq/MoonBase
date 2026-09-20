package stats

import "strings"

// Which caller a request came from, as one of one_d4's three words. The
// same question one_d4 answers for itself from its own headers (#1465),
// asked at the gateway so every service gets an answer.
//
// The two do not see the same evidence and will not reconcile. mcpserver
// reaches one_d4 on the compose network, so those calls are in no access
// log at all; what the gateway sees is the MCP client arriving at
// mcp.1d4.net. Edge source counts arrivals, service-local source counts
// work.
const (
	SourceMCP = "mcp"
	SourceUI  = "ui"
	SourceAPI = "api"
)

// The product token mcpserver sends on every call (OneD4Client).
const mcpserverAgent = "mcpserver"

// Every origin the Caddyfile grants Access-Control-Allow-Origin to. The
// header is the caller's to set, so the column is this bounded answer and
// never the header. deploy_config_test pins the list to the gateway.
var uiOrigins = map[string]bool{
	"http://localhost:5173": true,
	"https://1d4.net":       true,
	"https://iili.uk":       true,
	"https://muchq.com":     true,
	"https://tty1.uk":       true,
}

// SourceOf names the caller behind a request from the route it reached,
// the Origin the browser attached, and its User-Agent.
//
// The route is the MCP evidence rather than the host: it says the request
// reached the endpoint, so a scanner walking mcp.1d4.net is a direct
// caller and not an MCP client.
func SourceOf(route, origin, userAgent string) string {
	if strings.HasPrefix(userAgent, mcpserverAgent) {
		return SourceMCP
	}
	if route == mcpRoute {
		return SourceMCP
	}
	if uiOrigins[origin] {
		return SourceUI
	}
	return SourceAPI
}
