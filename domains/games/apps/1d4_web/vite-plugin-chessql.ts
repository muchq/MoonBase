import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { marked } from 'marked';
import type { Plugin } from 'vite';

/**
 * Serves CHESSQL.md to the app as HTML, converted here rather than in the
 * browser (#1425).
 *
 * The document is static, so a markdown renderer in the bundle would be a
 * parser shipped to every reader to produce the same bytes every time —
 * measured at 54 kB gzipped, against 21 kB for the rendered HTML. Converting
 * at build time also keeps marked a devDependency.
 *
 * A virtual module rather than an import of the .md path, because the file
 * lives outside this app's root and Vite's dev server refuses to serve those
 * (`Denied ID`) unless server.fs.allow is widened. Reading it here needs no
 * such grant.
 */
export const CHESSQL_REFERENCE_ID = 'virtual:chessql-reference';
const RESOLVED_ID = `\0${CHESSQL_REFERENCE_ID}`;

export const CHESSQL_SOURCE = fileURLToPath(
  new URL(
    '../../apis/one_d4/src/main/java/com/muchq/games/one_d4/docs/CHESSQL.md',
    import.meta.url,
  ),
);

export function renderReference(markdown: string): string {
  const html = marked.parse(markdown, { async: false, gfm: true });
  // Every roster is wider than a phone, and the wrapper is what the CSS
  // scrolls. Markdown tables cannot nest, and marked escapes anything inside a
  // fence, so these are the generated table elements and only those.
  return html
    .replaceAll('<table>', '<div class="table-scroll"><table>')
    .replaceAll('</table>', '</table></div>');
}

export default function chessqlReference(): Plugin {
  return {
    name: 'chessql-reference',
    resolveId(id) {
      return id === CHESSQL_REFERENCE_ID ? RESOLVED_ID : null;
    },
    load(id) {
      if (id !== RESOLVED_ID) return null;
      // Watched so `npm run dev` picks up an edit to the reference.
      this.addWatchFile(CHESSQL_SOURCE);
      const html = renderReference(readFileSync(CHESSQL_SOURCE, 'utf-8'));
      return `export default ${JSON.stringify(html)};`;
    },
  };
}
