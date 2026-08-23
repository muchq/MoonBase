import React from 'react';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { render, screen, cleanup } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { afterEach, describe, it, expect } from 'vitest';
import { MemoryRouter } from 'react-router';
import ChessQlView from '../views/ChessQlView';
import QueryView from '../views/QueryView';
import McpView from '../views/McpView';

// Read the same way McpView.test.tsx reaches mcp_tools.json: from the package
// root, because import.meta.url is not a file: URL under the jsdom transform.
const REFERENCE_PATH = resolve(
  process.cwd(),
  '../../apis/one_d4/src/main/java/com/muchq/games/one_d4/docs/CHESSQL.md',
);
const REFERENCE = readFileSync(REFERENCE_PATH, 'utf-8');

/** First column of each row of the markdown table under `heading`. */
function rosterUnder(heading: string, until: string): string[] {
  const start = REFERENCE.indexOf(heading);
  const end = REFERENCE.indexOf(until, start + heading.length);
  const section = REFERENCE.slice(start, end === -1 ? undefined : end);
  return [...section.matchAll(/^\|\s*`([^`]+)`/gm)].map((m) => m[1]);
}

// QueryView issues a useQuery on mount, so it needs a client even when the
// assertion is only about its syntax help.
function withProviders({ children }: { children: React.ReactNode }) {
  const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return (
    <QueryClientProvider client={qc}>
      <MemoryRouter>{children}</MemoryRouter>
    </QueryClientProvider>
  );
}

afterEach(cleanup);

describe('ChessQlView', () => {
  /**
   * The point of the page: it renders CHESSQL.md rather than a third copy of
   * the vocabulary. Asserting against the file read from disk means a field
   * added to the doc shows up here without anyone editing this app — and that
   * a build which stopped importing the doc fails rather than silently
   * serving a stale snapshot.
   *
   * Names only, never prose, for the reason ChessQlReferenceTest gives: the
   * doc stays free to be rewritten as documentation.
   */
  it('renders every field the reference lists', () => {
    render(<ChessQlView />);
    const fields = rosterUnder('## Fields', '### Date scoping');
    expect(fields.length).toBeGreaterThan(5);
    for (const field of fields) {
      expect(screen.getAllByText(field).length).toBeGreaterThan(0);
    }
  });

  it('renders every motif the reference lists', () => {
    render(<ChessQlView />);
    const motifs = [
      ...new Set(
        [...REFERENCE.matchAll(/`motif\(([a-z_]+)\)`/g)].map((m) => m[1]),
      ),
    ];
    expect(motifs.length).toBeGreaterThan(5);
    for (const motif of motifs) {
      expect(screen.getAllByText(new RegExp(motif)).length).toBeGreaterThan(0);
    }
  });

  it('renders the grammar and precedence sections the inline help omits', () => {
    render(<ChessQlView />);
    expect(
      screen.getByRole('heading', { name: /Grammar/ }),
    ).toBeInTheDocument();
    expect(
      screen.getByRole('heading', { name: /Operator Precedence/ }),
    ).toBeInTheDocument();
  });

  // Every roster is a wide table; the page must not scroll sideways because of
  // one. Pins the wrapper the CSS scrolls, which the build-time plugin adds.
  it('wraps each table so the table scrolls rather than the page', () => {
    const { container } = render(<ChessQlView />);
    const tables = container.querySelectorAll('table');
    expect(tables.length).toBeGreaterThan(0);
    for (const table of tables) {
      expect(table.parentElement?.className).toContain('table-scroll');
    }
  });
});

describe('links to the reference', () => {
  it('is reachable from the query page', () => {
    render(<QueryView />, { wrapper: withProviders });
    expect(
      screen.getByRole('link', { name: /Full reference/ }).getAttribute('href'),
    ).toBe('/chessql');
  });

  it('is reachable from the MCP page', () => {
    render(
      <MemoryRouter>
        <McpView />
      </MemoryRouter>,
    );
    expect(
      screen.getByRole('link', { name: 'ChessQL' }).getAttribute('href'),
    ).toBe('/chessql');
  });

  /**
   * The inline help used to carry the whole field and motif roster, which is
   * what #1425 calls out as hand-maintained and already rotted once. It is a
   * pointer now, so this asserts the roster is gone rather than duplicated —
   * the drift is removed, not guarded.
   */
  it('leaves the rosters to the reference instead of repeating them', () => {
    const { container } = render(<QueryView />, { wrapper: withProviders });
    const help = container.querySelector('.syntax-help');
    if (!help) throw new Error('syntax help not found');
    const motifs = [
      ...new Set(
        [...REFERENCE.matchAll(/`motif\(([a-z_]+)\)`/g)].map((m) => m[1]),
      ),
    ];
    const named = motifs.filter((m) => help.textContent?.includes(m));
    // One example is a cheat sheet; the roster is a copy.
    expect(named.length).toBeLessThanOrEqual(1);
  });
});
