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

// The tool roster the server actually advertises. McpToolRegistryContractTest (Java) pins the
// registry to this same file, so a tool added, removed or renamed on the server fails there
// first, and fails here until the page's table catches up. The page cannot ask the server
// directly — mcp.1d4.net sends no CORS headers — which is exactly why this loop exists.
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
   * The transport caveat is the reason a reader's standard MCP client will not connect, and it is
   * the kind of awkward sentence that gets tidied away. Pinned so removing it is deliberate.
   */
  it('states the endpoint and that it is not a standard MCP transport', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(screen.getAllByText(/mcp\.1d4\.net/).length).toBeGreaterThan(0);
    expect(screen.getByText(/no SSE/i)).toBeTruthy();
    expect(screen.getByText(/Mcp-Session-Id/)).toBeTruthy();
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
