import { Link } from 'react-router';
import { MCP_TOOLS } from '../mcpTools';

const ENDPOINT = 'https://mcp.1d4.net/mcp';

const LIST_TOOLS_CURL = `curl -s ${ENDPOINT} \\
  -H 'Content-Type: application/json' \\
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'`;

const CALL_TOOL_CURL = `curl -s ${ENDPOINT} \\
  -H 'Content-Type: application/json' \\
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call",
       "params":{"name":"index_chess_games",
                 "arguments":{"username":"hikaru","platform":"chess.com",
                              "start_month":"2026-06","end_month":"2026-06"}}}'`;

export default function McpView() {
  return (
    <div className="mcp-view">
      <section className="panel">
        <h2>MCP server</h2>
        <p>
          1d4 exposes its chess indexer and chess.com lookups as {MCP_TOOLS.length} tools over the
          Model Context Protocol, so an assistant can index a player, search the games with
          ChessQL, and aggregate them without a human in the loop.
        </p>
        <p className="panel-note">
          Endpoint: <code>{ENDPOINT}</code> — no account, no API key.
        </p>
      </section>

      <section className="panel">
        <h2>Connecting</h2>
        <p>
          The endpoint speaks <strong>JSON-RPC 2.0 over a single HTTP POST</strong>. Each request
          is self-contained: send a method, get a JSON response.
        </p>
        <pre className="code-block">
          <code>{LIST_TOOLS_CURL}</code>
        </pre>
        <p>
          That is the whole protocol surface — which is also the caveat. It is{' '}
          <strong>not</strong> one of MCP&rsquo;s standard remote transports: there is no SSE
          stream, no <code>Mcp-Session-Id</code>, and <code>notifications/initialized</code> is not
          implemented. Clients that speak stdio, HTTP+SSE or Streamable HTTP will not connect
          without a small bridge that turns their transport into these plain POSTs. If you are
          driving it from your own code, the <code>curl</code> above is the entire contract.
        </p>
        <p className="panel-note">
          <code>initialize</code>, <code>tools/list</code> and <code>tools/call</code> are the
          three methods implemented. Calls are unauthenticated today; the server supports a bearer
          token, but the deployment sets none.
        </p>
      </section>

      <section className="panel">
        <h2>Index first</h2>
        <p>
          The searching tools only see games that have been indexed. Nothing is indexed
          speculatively, so the order matters:
        </p>
        <ol className="mcp-steps">
          <li>
            <code>index_chess_games</code> — asks for a player and a month range. Returns a request
            id immediately; the work happens in the background.
          </li>
          <li>
            <code>index_status</code> — poll that id until it reports completed. A month already
            indexed is skipped, so re-running is cheap.
          </li>
          <li>
            <code>query_chess_games</code> / <code>aggregate_chess_games</code> — now they have
            something to read.
          </li>
        </ol>
        <p className="panel-note">
          <code>analyze_position</code> is the exception: hand it a PGN and it detects motifs in
          that one game, with no indexing at all.
        </p>
      </section>

      <section className="panel">
        <h2>Tools</h2>
        <div className="table-wrap">
          <table className="mcp-tools-table">
            <thead>
              <tr>
                <th>Tool</th>
                <th>What it does</th>
              </tr>
            </thead>
            <tbody>
              {MCP_TOOLS.map((tool) => (
                <tr key={tool.name}>
                  <td className="nowrap">
                    <code>{tool.name}</code>
                    {tool.needsIndex && (
                      <span className="mcp-needs-index" title="Reads indexed games">
                        indexed
                      </span>
                    )}
                  </td>
                  <td>{tool.summary}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="panel-note">
          Each tool advertises its own JSON input schema through <code>tools/list</code>; the
          descriptions there are what an assistant actually reads.
        </p>
      </section>

      <section className="panel">
        <h2>A worked example</h2>
        <p>Index one month, then ask two questions about it.</p>
        <pre className="code-block">
          <code>{CALL_TOOL_CURL}</code>
        </pre>
        <p>
          Poll <code>index_status</code> with the returned id until it is completed, then query the
          indexed games:
        </p>
        <ul className="mcp-examples">
          <li>
            <code>query_chess_games</code> with{' '}
            <code>
              {'(white.username = "hikaru" OR black.username = "hikaru") AND motif(fork)'}
            </code>{' '}
            — their games containing a fork.
          </li>
          <li>
            <code>aggregate_chess_games</code> with the same filter and{' '}
            <code>group_by: ["opening_family"]</code> — the openings they played, by count.
          </li>
        </ul>
        <p className="panel-note">
          Both sides of the OR, or you only count the games they had White. Naming a{' '}
          <code>player</code> instead is for perspective fields (<code>me.color</code>,{' '}
          <code>outcome</code>, <code>opponent.elo</code>); on its own it does not restrict an
          aggregate to that player, and an aggregate that would not be scoped is refused rather
          than silently counting everyone.
        </p>
      </section>

      <section className="panel">
        <h2>ChessQL</h2>
        <p>
          <code>query_chess_games</code> and the filter half of <code>aggregate_chess_games</code>{' '}
          take ChessQL — the same language the <Link to="/query">Query</Link> page uses, where the
          field and motif reference lives.
        </p>
      </section>
    </div>
  );
}
