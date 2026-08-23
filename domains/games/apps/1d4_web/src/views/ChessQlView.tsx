import reference from 'virtual:chessql-reference';

/**
 * The ChessQL reference (#1425) — CHESSQL.md itself, not a copy of it.
 *
 * The same file mcpserver serves as `chessql://reference` (#1326), so browser
 * and MCP readers cannot disagree, and ChessQlReferenceTest’s pinning of the
 * field and motif tables against SqlCompiler covers this page for free.
 *
 * The HTML is generated from a checked-in document at build time by
 * vite-plugin-chessql, so it is a constant in the bundle: there is no input
 * here for innerHTML to be dangerous with.
 */
export default function ChessQlView() {
  return (
    <div
      className="chessql-reference"
      dangerouslySetInnerHTML={{ __html: reference }}
    />
  );
}
