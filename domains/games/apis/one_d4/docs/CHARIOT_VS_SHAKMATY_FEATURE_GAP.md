# Chariot (Java) vs Shakmaty (Rust) — Feature Gap for Indexing Rewrite

This document compares **chariot** (`io.github.tors42:chariot`, Java) and **shakmaty** (Rust, with **pgn-reader**) for the one_d4 indexing pipeline: PGN reading, position replay, and motif/feature extraction. It summarizes what the current pipeline uses, what Phase 9 plans to use, and whether shakmaty covers those needs so you can assess the cost of a Rust rewrite.

---

## What one_d4 Uses Today (Chariot)

### 1. PGN movetext → list of SAN moves

- **Chariot:** `PGN.Text.parse(moveText)` returns a stream of tokens; filter `PGN.Text.Move`, get `.san()` per move.
- **Location:** `GameReplayer.extractMoves()`.
- **Note:** NAGs (e.g. `$1`) can leak through as Move tokens with invalid SAN; `isValidSan()` filters them before replay.

### 2. Replay and FEN generation

- **Chariot:** `Board.ofStandard()`, then for each SAN: `board = board.play(san)`, and `board.toFEN()` after each move.
- **Location:** `GameReplayer.replay()`.
- **Output:** `List<PositionContext>` with `(moveNumber, fen, whiteToMove, lastMove)`.

### 3. Board model for detectors (not Chariot)

- Detectors do **not** use Chariot’s `Board` for analysis.
- They take the FEN from `PositionContext`, parse only the **placement** (first segment) via `PinDetector.parsePlacement()` into an `int[][]` (8×8, piece codes ±1..±6).
- All piece logic (attacks, pins, who gives check, etc.) is in **BoardUtils** and detector code: `pieceAttacks()`, `isPathClear()`, `countAttackers()`, `findKing()`, `findCheckingPiece()`, `squareName()`, `pieceNotation()`, `destinationSquare()`, `parsePromotionDestination()`.

So for **current** motif detection, Chariot is used only for: (a) parsing movetext into SANs, and (b) replaying SANs to produce a sequence of FENs.

---

## What Phase 9 Plans to Use (Chariot)

From `ROADMAP.md` and `DATAFUSION_PARQUET.md`:

- **Check attribution:** Distinguish “promotion with check” (promoted piece gives check) vs “discovered check” (another piece gives check). Requires knowing **which piece(s) give check** after each move.
- **Double check:** Two pieces give check at once.
- **New motifs:** back rank mate, smothered mate, zugzwang, double check, overloaded piece — several need full board state and “who gives check” style queries.

So the only **additional** Chariot dependency planned is: **query which piece(s) are giving check** (and possibly “did the checker move this half-move?”). The rest is still FEN/placement + custom logic or Chariot’s board if it exposes it.

---

## Shakmaty + pgn-reader Capabilities

- **Chess vocabulary & position:** `Chess` (default start), `Position` trait, `board()`, `piece_at(Square)`, `king_of(Color)`, FEN parse/write, SAN parse and `San::to_move(&pos)`, `play(m)` / `play_unchecked(m)`.
- **Game state:** `is_check()`, `is_checkmate()`, `is_stalemate()`, `outcome()`, `legal_moves()`, etc.
- **Check attribution:** `checkers()` → **Bitboard of squares of pieces giving check**. With `board().piece_at(sq)` you get exactly “which pieces give check.” So: promoted-piece check vs discovered check vs double check is implementable.
- **Attacks / rays:** `attacks::attacks(piece, square, occupied)`, `bishop_attacks`, `rook_attacks`, `queen_attacks`, `knight_attacks`, `king_attacks`, `between()`, `ray()`, `aligned()`.
- **FEN:** `fen::Fen` parse, `Epd::from_position()`, `Board::from_str()` for placement-only; full position via `Fen::into_position(CastlingMode::Standard)`.
- **Variants:** Optional `variant` feature; Lichess variants supported.
- **pgn-reader:** Streaming, visitor-based PGN reader; `Visitor::san(&mut self, movetext, san_plus)` receives `SanPlus` (SAN + check/checkmate suffix). Integrates with shakmaty: e.g. `san_plus.san.to_move(&pos)` then `pos.play_unchecked(m)` to replay. No move legality in the reader itself (you validate via shakmaty when replaying).

---

## Feature-by-Feature Comparison

| Need | Chariot | Shakmaty + pgn-reader | Gap? |
|------|--------|------------------------|------|
| Parse movetext → SAN list | `PGN.Text.parse()` → Move tokens → `.san()` | pgn-reader `Visitor::san(_, _, san_plus)`; collect SAN or replay in visitor | **No.** Different style (visitor vs stream); same capability. Filter invalid SAN/NAG in visitor or when replaying. |
| Start position | `Board.ofStandard()` | `Chess::default()` / `Chess::new()` | **No.** |
| Play SAN, get next position | `board.play(san)` (returns new Board) | `san.to_move(&pos)?` then `pos.play(m)` or `play_unchecked` | **No.** |
| FEN after each move | `board.toFEN()` | `Fen::from_position()` / `Epd::from_position()` or board FEN from `pos.board()` | **No.** |
| Which pieces give check (Phase 9) | Assumed: query board for checkers | `pos.checkers()` → Bitboard; iterate squares, `pos.board().piece_at(sq)` | **No.** Shakmaty gives checker squares directly. |
| Double check | — | `pos.checkers().count() >= 2` | **No.** |
| Piece at square / king square | Not used in detectors (custom int[][]) | `board.piece_at(sq)`, `board.king_of(color)` | **No.** |
| Sliding/ray attacks for pins, skewers, etc. | Custom in BoardUtils | `attacks::*` with `Bitboard` occupancy | **No.** Can reimplement detector logic with bitboards or same ideas. |
| PGN headers | one_d4 uses custom regex in `PgnParser` | pgn-reader `Visitor::tag()`; no change to “headers separate from movetext” | **No.** |
| Lichess API client | Chariot is a Lichess API client (HTTP, auth, etc.) | N/A (shakmaty is not an API client) | **Only if** the Rust indexer must talk to Lichess API. For indexing from PGN (e.g. dumps or one_d4-provided PGN), **no gap**. |

---

## Summary: Cost of a Rust Rewrite (Library Side)

- **Current pipeline:** Chariot is used only for (1) SAN extraction from movetext and (2) replay → FEN sequence. All motif logic uses custom FEN placement parsing + BoardUtils. **Shakmaty + pgn-reader** can cover (1) and (2) and provide a better foundation for (3) by using `Position` + `checkers()` + `attacks::*` instead of hand-rolled int[][] and helpers.
- **Phase 9 (check attribution, double check, new motifs):** Requires “which pieces give check” and full board state. **Shakmaty** provides `checkers()`, `board()`, `piece_at`, `king_of`, and full attack/ray APIs, so there is **no feature gap** vs the planned Chariot usage.
- **Real cost** of the rewrite is **reimplementing the detector logic** (pin, skewer, fork, attack, check, promotion, back-rank mate, smothered mate, etc.) in Rust using shakmaty’s types (and optionally bitboards), not missing library features. You may also need to replicate the current “last move” / SAN context (e.g. destination square, promotion square) from the replayed move or SAN string, which is straightforward with shakmaty’s `Move` and SAN.

---

## References

- Chariot: https://github.com/tors42/chariot (Java Lichess API client; one_d4 uses `chariot.chess.Board` and `chariot.model.PGN`).
- Shakmaty: https://docs.rs/shakmaty/latest/shakmaty/ (position, moves, FEN, SAN, UCI, attacks, variants).
- pgn-reader: https://docs.rs/pgn-reader/latest/pgn_reader/ (streaming PGN, visitor, SAN via `SanPlus`; integrates with shakmaty for `to_move` and replay).
