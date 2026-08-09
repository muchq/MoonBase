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
   * Connecting is one command, and its whole job is to be copy-pasteable (#1325). A command that
   * renders but names the wrong endpoint — or names none — is worse than no command at all,
   * because it looks like it works.
   *
   * Asserts the transport flag too: `claude mcp add` without `--transport http` registers a stdio
   * server that tries to execute the URL as a program, which fails in a way that reads like the
   * server being down rather than the line being wrong.
   */
  it('gives a one-line connect command naming the endpoint', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    const connecting = screen.getByRole('heading', { name: /connecting/i })
      .parentElement as HTMLElement;
    const text = connecting.textContent ?? '';

    expect(text).toContain('claude mcp add');
    expect(text).toContain('--transport http');
    expect(text).toContain('https://mcp.1d4.net/mcp');
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
   * The MCP tools act on the corpus the site serves — they are calls to the same API (#1332).
   * This page used to say the opposite, correctly, because the server carried its own in-memory
   * index. A visitor deciding whether to index here or on the Index page acts on this paragraph,
   * so it is pinned rather than left to whoever edits the copy next.
   */
  it('says the corpus is shared with the site rather than private to this server', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );

    expect(
      screen.getByRole('heading', { name: /one corpus, shared with the site/i }),
    ).toBeTruthy();
    expect(screen.getByText(/the same corpus/i)).toBeTruthy();
    // The stale claim, named so it cannot quietly come back while the heading changes.
    expect(screen.queryByText(/in-memory database/i)).toBeNull();
    expect(screen.queryByText(/discarded when the process restarts/i)).toBeNull();
  });

  /**
   * index_chess_games is synchronous for a single month — one_d4's POST /v1/index is async, and
   * the adapter polls it to completion — and queued for a range. The page's worked example is one
   * month, so copy that says "poll until it completes" would send readers after something already
   * finished.
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
