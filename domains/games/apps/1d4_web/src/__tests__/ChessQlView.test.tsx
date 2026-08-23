import React from 'react';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { render, screen, cleanup } from '@testing-library/react';
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { afterEach, describe, it, expect } from 'vitest';
import { MemoryRouter } from 'react-router';
import App from '../App';
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

/**
 * First column of each row of the markdown table between two headings — the
 * same shape ChessQlReferenceTest parses on the Java side, and bounded the same
 * way, because the rosters are separate tables and a run to end-of-document
 * would sweep up unrelated backticked cells.
 */
function rosterBetween(heading: string, until: string): string[] {
  const start = REFERENCE.indexOf(heading);
  if (start === -1) throw new Error(`CHESSQL.md has no ${heading}`);
  const end = REFERENCE.indexOf(until, start + heading.length);
  const section = REFERENCE.slice(start, end === -1 ? undefined : end);
  return [...section.matchAll(/^\|\s*`([^`]+)`/gm)].map((m) => m[1]);
}

const MOTIFS = [
  ...new Set([...REFERENCE.matchAll(/`motif\(([a-z_]+)\)`/g)].map((m) => m[1])),
];

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
   * added to the doc shows up here without anyone editing this app, and that a
   * build which stopped importing the doc — or mangled part of it — fails
   * rather than silently serving something stale.
   *
   * All three rosters, because ChessQlReferenceTest pins all three against the
   * compiler and this page claims to inherit that. Names only, never prose,
   * for the reason that test gives: the doc stays free to be rewritten as
   * documentation.
   */
  it('renders every field the reference lists', () => {
    render(<ChessQlView />);
    const fields = rosterBetween('## Fields', '### Date scoping');
    expect(fields.length).toBeGreaterThan(5);
    for (const field of fields) {
      expect(screen.getAllByText(field).length).toBeGreaterThan(0);
    }
  });

  it('renders every perspective field the reference lists', () => {
    render(<ChessQlView />);
    const perspective = rosterBetween('### Perspective fields', '## Motifs');
    expect(perspective.length).toBeGreaterThan(2);
    for (const field of perspective) {
      expect(screen.getAllByText(field).length).toBeGreaterThan(0);
    }
  });

  /**
   * Exact `motif(x)` text, not a substring match on the bare name: `pin` is
   * inside `cross_pin`, `check` inside `checkmate` and `double_check`, and
   * `promotion` inside `promotion_with_check`. A loose match lets a page that
   * dropped the short motifs still pass on its longer siblings.
   */
  it('renders every motif the reference lists', () => {
    render(<ChessQlView />);
    expect(MOTIFS.length).toBeGreaterThan(5);
    for (const motif of MOTIFS) {
      expect(screen.getAllByText(`motif(${motif})`).length).toBeGreaterThan(0);
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

  /**
   * CHESSQL.md links to its own sections. marked emits no heading ids of its
   * own, so without the gfmHeadingId extension every one of those links is a
   * no-op in the browser — which MCP clients never see, because they get the
   * markdown and slugify it themselves.
   */
  it('resolves its own in-document links', () => {
    const { container } = render(<ChessQlView />);
    const targets = [...container.querySelectorAll('a[href^="#"]')].map((a) =>
      (a.getAttribute('href') ?? '').slice(1),
    );
    expect(targets.length).toBeGreaterThan(0);
    for (const target of targets) {
      expect(container.querySelector(`[id="${target}"]`)).not.toBeNull();
    }
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

describe('the /chessql route', () => {
  /**
   * Mounted through App, because the links below only assert where they point.
   * An unregistered route is not a 404 here — the catch-all sends it to
   * /games — so without this a missing Route is a silently wrong destination.
   */
  it('serves the reference at /chessql', () => {
    const qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
    const { container } = render(
      <QueryClientProvider client={qc}>
        <MemoryRouter initialEntries={['/chessql']}>
          <App />
        </MemoryRouter>
      </QueryClientProvider>,
    );
    expect(container.querySelector('.chessql-reference')).not.toBeNull();
    expect(
      screen.getByRole('heading', { name: /Operator Precedence/ }),
    ).toBeInTheDocument();
  });

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
   * the drift is removed, not guarded. Both rosters, because the issue names
   * both and a field list rots the same way a motif list does.
   */
  it('leaves the rosters to the reference instead of repeating them', () => {
    const { container } = render(<QueryView />, { wrapper: withProviders });
    const help = container.querySelector('.syntax-help');
    if (!help) throw new Error('syntax help not found');
    const text = help.textContent ?? '';

    const namedMotifs = MOTIFS.filter((m) => text.includes(m));
    const fields = rosterBetween('## Fields', '### Date scoping');
    const namedFields = fields.filter((f) => text.includes(f));

    // The cheat sheet spends one motif (motif(fork)) and four field names:
    // white.elo for a numeric comparison, eco for a quoted string, and date
    // and month, which QueryView.test.tsx separately requires an example of.
    // The doc's rosters are 17 fields and 16 motifs, so these bounds catch a
    // roster coming back without forbidding the examples that make the help
    // useful.
    expect(namedMotifs.length).toBeLessThanOrEqual(1);
    expect(namedFields.length).toBeLessThanOrEqual(4);
  });
});
