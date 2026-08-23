import { Link } from 'react-router';
import { MCP_TOOLS } from '../mcpTools';

const ENDPOINT = 'https://mcp.1d4.net/mcp';

const ADD_COMMAND = `claude mcp add --transport http 1d4 ${ENDPOINT}`;

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
          1d4 exposes its chess indexer and chess.com lookups as{' '}
          {MCP_TOOLS.length} tools over the Model Context Protocol, so an
          assistant can index a player, search the games with ChessQL, and
          aggregate them without a human in the loop.
        </p>
        <p className="panel-note">
          Endpoint: <code>{ENDPOINT}</code> — no account, no API key.
        </p>
      </section>

      <section className="panel">
        <h2>One corpus, shared with the site</h2>
        <p>
          What you index through these tools is <strong>the same corpus</strong>{' '}
          1d4.net shows. The MCP server has no database of its own: the indexing
          and search tools are calls to the same API behind the{' '}
          <Link to="/games">Games</Link> page and <code>api.1d4.net</code>.
        </p>
        <p className="panel-note">
          So a player you index here shows up on the site, and a query here sees
          everything the <Link to="/index">Index</Link> page has already
          collected — there is no separate index to fill first.
        </p>
      </section>

      <section className="panel">
        <h2>Connecting</h2>
        <p>Streamable HTTP, so a client points straight at it:</p>
        <pre className="code-block">
          <code>{ADD_COMMAND}</code>
        </pre>
        <p className="panel-note">
          No key, no bridge. The optional server&rarr;client SSE stream is not
          implemented, so <code>GET /mcp</code> answers <code>405</code>.
        </p>
      </section>

      <section className="panel">
        <h2>Or drive it yourself</h2>
        <p>
          It is JSON-RPC 2.0 over HTTP POST, so a request is also just a{' '}
          <code>curl</code>:
        </p>
        <pre className="code-block">
          <code>{LIST_TOOLS_CURL}</code>
        </pre>
      </section>

      <section className="panel">
        <h2>Index first</h2>
        <p>
          The searching tools only see games that have been indexed. Nothing is
          indexed speculatively, so the order matters:
        </p>
        <ol className="mcp-steps">
          <li>
            <code>index_chess_games</code> — asks for a player and a month
            range. A <strong>single month is indexed while you wait</strong> and
            comes back completed; a multi-month range is queued and comes back
            with a request id.
          </li>
          <li>
            <code>index_status</code> — for a queued range, poll that id until
            it reports completed. A month already indexed is skipped, so
            re-running is cheap.
          </li>
          <li>
            <code>query_chess_games</code> / <code>aggregate_chess_games</code>{' '}
            — now they have something to read.
          </li>
        </ol>
        <p className="panel-note">
          <code>analyze_position</code> is the exception: hand it a PGN and it
          detects motifs in that one game, with no indexing at all.
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
                      <span
                        className="mcp-needs-index"
                        title="Reads indexed games"
                      >
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
          Each tool advertises its own JSON input schema through{' '}
          <code>tools/list</code>; the descriptions there are what an assistant
          actually reads.
        </p>
      </section>

      <section className="panel">
        <h2>A worked example</h2>
        <p>Index one month, then ask two questions about it.</p>
        <pre className="code-block">
          <code>{CALL_TOOL_CURL}</code>
        </pre>
        <p>
          That is one month, so it returns completed — nothing to poll. (Ask for
          a range and you get a request id instead, which is what{' '}
          <code>index_status</code> is for.) Then query what it indexed:
        </p>
        <ul className="mcp-examples">
          <li>
            <code>query_chess_games</code> with{' '}
            <code>
              {
                '(white.username = "hikaru" OR black.username = "hikaru") AND motif(fork)'
              }
            </code>{' '}
            — their games containing a fork.
          </li>
          <li>
            <code>aggregate_chess_games</code> with the same filter and{' '}
            <code>group_by: ["opening_family"]</code> — the openings they
            played, by count.
          </li>
          <li>
            The same call with <code>player: &quot;hikaru&quot;</code>,{' '}
            <code>order_by: &quot;score&quot;</code> and{' '}
            <code>min_games: 10</code> — the openings they score best in, since
            each group carries wins/losses/draws and score once a player is
            named.
          </li>
        </ul>
        <p className="panel-note">
          Both sides of the OR, or you only count the games they had White.
          Naming a <code>player</code> instead is for perspective fields (
          <code>me.color</code>, <code>outcome</code>, <code>opponent.elo</code>
          ); on its own it does not restrict an aggregate to that player, and an
          aggregate that would not be scoped is refused rather than silently
          counting everyone.
        </p>
      </section>

      <section className="panel">
        <h2>ChessQL</h2>
        <p>
          <code>query_chess_games</code> and the filter half of{' '}
          <code>aggregate_chess_games</code> take ChessQL — the same language
          the <Link to="/query">Query</Link> page uses. The full reference is
          at <Link to="/chessql">ChessQL</Link>.
        </p>
        <p>
          The server publishes that same reference over MCP, as the{' '}
          <code>chessql://reference</code> resource: grammar, operator
          precedence, the field and motif rosters, perspective fields and NULL
          semantics. It is a <em>resource</em> rather than an eleventh tool, so
          a client attaches it as context instead of spending a call to learn
          the syntax. Clients that do not read resources still get the
          vocabulary from each tool&apos;s description.
        </p>
      </section>
    </div>
  );
}
