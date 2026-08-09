import React from 'react';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { render, screen, within } from '@testing-library/react';
import { afterEach, describe, it, expect } from 'vitest';
import { cleanup } from '@testing-library/react';
import { MemoryRouter } from 'react-router';
import App from '../App';
import McpView from '../views/McpView';
import { MCP_TOOLS } from '../mcpTools';

// The tool roster the server actually advertises. McpToolRosterContractTest (Java) pins what the
// running server returns from tools/list to this same file, so a tool added, removed or renamed
// on the server fails there first, and fails here until the page's table catches up. mcp.1d4.net
// allows this origin, so the page could fetch the list instead; it deliberately does not, so the
// drift is caught in CI rather than in production. See mcpTools.ts.
// Resolved from the package root (vitest's cwd), because import.meta.url is not a file: URL
// under the jsdom transform.
const ADVERTISED_TOOLS: { tools: string[] } = JSON.parse(
  readFileSync(
    resolve(
      process.cwd(),
      '../../apis/mcpserver/src/test/resources/mcp_tools.json',
    ),
    'utf-8',
  ),
);

afterEach(cleanup);

describe('McpView', () => {
  it('documents exactly the tools the server advertises', () => {
    const documented = MCP_TOOLS.map((t) => t.name).sort();
    expect(documented).toEqual([...ADVERTISED_TOOLS.tools].sort());
  });

  it('renders a row per tool, each with a description', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    // Scoped to the table: several tool names also appear in the prose above it.
    const table = within(screen.getByRole('table'));
    for (const tool of MCP_TOOLS) {
      const row = table.getByText(tool.name).closest('tr');
      expect(row).not.toBeNull();
      // The name alone is not documentation: the row must also carry the summary.
      expect(within(row as HTMLElement).getByText(tool.summary)).toBeTruthy();
    }
  });

  /**
   * The index-first ordering is the single thing a newcomer gets wrong, so it is not allowed to
   * quietly vanish from the page.
   */
  it('tells the reader to index before querying', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(screen.getByRole('heading', { name: /index first/i })).toBeTruthy();
    expect(
      screen.getByText(/only see games that have been indexed/i),
    ).toBeTruthy();
  });

  /**
   * The connection section's job is to be copy-pasteable (#1325). A config block that renders but
   * names no endpoint is worse than no config at all, because it looks like it works.
   */
  it('gives a copy-pasteable client config naming the endpoint', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(screen.getAllByText(/mcp\.1d4\.net/).length).toBeGreaterThan(0);

    const configs = screen
      .getAllByText(/mcpServers/)
      .map((el) => el.textContent ?? '');
    expect(configs.length).toBeGreaterThan(0);
    // Every config block must actually point at the deployment, not just look like JSON.
    for (const config of configs) {
      expect(config).toContain('https://mcp.1d4.net/mcp');
    }
    // And one of them must be the no-bridge case: a direct http client entry.
    expect(configs.some((c) => /"type":\s*"http"/.test(c))).toBe(true);
  });

  /**
   * A config block is only copy-pasteable if the reader knows where to paste it, and these two are
   * not interchangeable: `{"type":"http","url":...}` is Claude Code's `.mcp.json` schema, while
   * Claude Desktop's `claude_desktop_config.json` takes command/args and reaches a remote server
   * through Custom Connectors. Pasting the http block into the Desktop file yields a server that
   * silently never loads, so an unlabelled block is a wrong answer that looks like a right one.
   *
   * Pins the destinations rather than the prose: the failure this guards is a block losing its
   * label, which every other test here would still pass.
   */
  it('says which client each config block is for, and that the http one is not Desktop', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    const connecting = screen.getByRole('heading', { name: /connecting/i })
      .parentElement as HTMLElement;
    const text = connecting.textContent ?? '';

    expect(text).toMatch(/Claude Code/);
    expect(text).toMatch(/\.mcp\.json/);
    expect(text).toMatch(/Claude Desktop/);
    // The correction that matters: Desktop's file cannot take the http entry, and the route that
    // does work is named.
    expect(text).toMatch(/claude_desktop_config\.json/);
    expect(text).toMatch(/custom connector/i);
  });

  /**
   * The one thing the transport does not do. Streamable HTTP makes the server->client SSE leg
   * optional, and micronaut-mcp does not implement it, so GET /mcp is a 405 rather than a stream.
   * A reader debugging a client that probes GET needs to find that here rather than guess.
   */
  it('says the server-to-client SSE stream is not implemented', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(screen.getByText(/SSE stream is not implemented/i)).toBeTruthy();
  });

  /**
   * The MCP server indexes into its own in-memory database — empty at boot, discarded on restart,
   * and not the corpus behind Games/api.1d4.net. A page that sits in the same nav and talks about
   * indexing invites exactly the wrong conclusion, so the fact is pinned rather than left to
   * whoever edits the copy next.
   */
  it('says its index is separate from the site corpus and does not survive restarts', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(
      screen.getByRole('heading', { name: /its index is its own/i }),
    ).toBeTruthy();
    expect(screen.getByText(/in-memory database/i)).toBeTruthy();
    expect(
      screen.getByText(/discarded when the process restarts/i),
    ).toBeTruthy();
  });

  /**
   * index_chess_games is synchronous for a single month (IndexerFacade goes through
   * submitHybrid) and queued for a range. The page's worked example is one month, so copy that
   * says "poll until it completes" would be telling readers to poll something already finished.
   */
  it('describes single-month indexing as synchronous and ranges as polled', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(
      screen.getByText(/single month is indexed while you wait/i),
    ).toBeTruthy();
    expect(screen.getByText(/multi-month range is queued/i)).toBeTruthy();
    // And the worked example, which is a single month, must not send the reader off to poll.
    expect(screen.getByText(/nothing to poll/i)).toBeTruthy();
  });

  it('is reachable at /mcp and marks its nav link active', () => {
    render(
      <MemoryRouter initialEntries={['/mcp']}>
        <App />
      </MemoryRouter>,
    );

    expect(screen.getByRole('heading', { name: /MCP server/i })).toBeTruthy();
    const link = screen.getByRole('link', { name: 'MCP' });
    expect(link.className).toContain('active');
  });
});
