package stats

// Which caller a request came from, as one of one_d4's three words. The
// same question one_d4 answers for itself from its own headers (#1465),
// asked at the gateway so every service gets an answer.
//
// api is the residue and not a caller class: it is every request that did
// not reach /mcp and did not come from a browser on a granted origin, so a
// human reading git.muchq.com and a scanner walking it are both api.
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
// the Origin attached to it, and the class of agent that sent it.
//
// Both inputs a caller controls are corroborated by something it does not.
// The route is the MCP evidence rather than the User-Agent: mcpserver's own
// calls never traverse the gateway, so a request claiming its product token
// here is by construction not mcpserver, while reaching /mcp is a fact
// about where the request arrived. Likewise a granted Origin only means the
// web app when a browser sent it — curl will copy any header asked of it,
// and a row reading "the web app, via curl" is a row that says nothing.
func SourceOf(route, origin, agentClass string) string {
	if route == mcpRoute {
		return SourceMCP
	}
	if agentClass == AgentBrowser && uiOrigins[origin] {
		return SourceUI
	}
	return SourceAPI
}
