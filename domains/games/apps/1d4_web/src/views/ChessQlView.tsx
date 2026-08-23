import reference from 'virtual:chessql-reference';

/**
 * The ChessQL reference (#1425) — rendered from CHESSQL.md rather than from a
 * copy of its vocabulary.
 *
 * Same source file mcpserver reads for `chessql://reference` (#1326), so
 * ChessQlReferenceTest’s pinning of the field, perspective and motif tables
 * against SqlCompiler reaches this page too. Not the same bytes: MCP serves
 * the markdown from the API jar at its own boot, this is HTML snapshotted at
 * Vite build into a different deploy unit, so an edit to the doc reaches the
 * two at whatever times they each ship. What holds is that neither is a third
 * roster anyone maintains by hand.
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
