/**
 * The tools mcp.1d4.net advertises, as documented on /mcp.
 *
 * The roster is checked in rather than fetched. mcp.1d4.net allows this origin, so a tools/list
 * call from a browser on 1d4.net would work — but a build-time contract fails in CI, before a
 * deploy, where a runtime fetch can only ever be wrong in production, and would put an empty
 * table on the page whenever the MCP server is down.
 *
 * Pinned from both ends, because a hand-maintained table would otherwise rot the first time a
 * tool is added or renamed. `McpToolRosterContractTest` (Java) asserts what the server actually
 * advertises over tools/list matches mcp_tools.json, and `McpView.test.tsx` asserts the names
 * below match that same file.
 */
export type McpToolDoc = {
  name: string;
  summary: string;
  /** True when the tool reads indexed games, i.e. it needs index_chess_games to have run. */
  needsIndex?: boolean;
};

export const MCP_TOOLS: McpToolDoc[] = [
  {
    name: 'index_chess_games',
    summary:
      "Index a player's games. A single month runs while you wait; a multi-month range returns a request id to poll.",
  },
  {
    name: 'index_status',
    summary:
      'Poll a multi-month indexing request: status, how many games have landed, and any error message.',
  },
  {
    name: 'query_chess_games',
    summary:
      'Search indexed games with ChessQL, including motif and sequence predicates.',
    needsIndex: true,
  },
  {
    name: 'aggregate_chess_games',
    summary:
      'Grouped counts over indexed games — "most popular openings" in one call instead of paging every row.',
    needsIndex: true,
  },
  {
    name: 'analyze_position',
    summary:
      'Detect motifs in a single PGN. The one indexer tool that needs no indexing first.',
  },
  {
    name: 'chess_com_games',
    summary:
      "A player's chess.com games for one month, filterable by time class, colour, rated and opponent.",
  },
  {
    name: 'chess_com_player',
    summary: "A player's chess.com profile, including their title.",
  },
  {
    name: 'chess_com_players',
    summary: 'Batch profile lookup, up to 50 usernames per call.',
  },
  {
    name: 'chess_com_stats',
    summary: "A player's chess.com rating stats per time class.",
  },
  {
    name: 'server_time',
    summary:
      'Current UTC time, for grounding relative dates like "last month".',
  },
];
