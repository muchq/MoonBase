# Chariot Capabilities and Plan for Detector Integration

Phase 9 (issue #1049) called for using the Chariot library for check attribution and motif accuracy. That work was not completed: detectors still use FEN parsing + hand-rolled `BoardUtils` only. This document analyzes Chariot’s actual API and proposes a concrete plan to leverage it.

---

## 1. Chariot Capabilities (0.2.6)

### 1.1 Public API

**`chariot.chess.Board`** (used by `GameReplayer` today):

| Method / behavior | Purpose |
|-------------------|--------|
| `Board.ofStandard()`, `Board.fromFEN(String fen)` | Create position |
| `board.play(Move)`, `board.play(String... uciOrSan)` | Replay moves; returns new Board |
| `board.toFEN()` | FEN string (current use: populate `PositionContext`) |
| `board.validMoves()` | All legal moves in current position |
| `board.sideToMove()` | Who moves (default from FEN) |
| `board.moveNum()` | Full move number from FEN |

**Not in public Board:** No `isCheck()`, `isCheckmate()`, `checkers()`, or “squares attacked by piece.” So we cannot ask Chariot “which pieces give check?” from the public API.

**`chariot.chess.DefaultBoard`** (extends `PieceTypedBoard<Piece>`):

| Method | Purpose |
|--------|--------|
| `DefaultBoard.of(Board board)` | Wrap any Board (e.g. after replay) |
| `squares().get(pos)`, `squares().all()` | Piece or empty per square |
| `pieces().all()`, `pieces().all(Side)`, `pieces().of(Piece type, Side side)` | Typed piece list |
| `historyFEN()` | FEN after initial + after each move (incl. current) |
| `historyMove()` | **UCI moves** (e.g. `"e2e4"`, `"e7e8q"`) — gives exact from/to and promotion |

So from Chariot we get: **authoritative position (squares/pieces), move history as UCI, and FEN** — but no check/checkmate/attack queries.

### 1.2 Internal API (not for direct use)

**`chariot.internal.chess.NaiveChess`** implements `Board`. It contains the logic we need but in an internal package:

- **`squaresAttackedByPiece(Square.With<Piece> piece)`** — used to compute which squares a piece attacks (for legal moves and for SAN generation).
- **Check/checkmate for SAN:** In `toSAN()`, after `_play(internalMove)` it determines `inCheck` by checking whether any piece of the side that just moved attacks the opponent king: `boardIfPlayed.piecesMatching(...).anyMatch(candidate -> boardIfPlayed.squaresAttackedByPiece(candidate).anyMatch(square -> square.equals(king.pos())))`. So “who gives check” and “# vs +” are computed internally but **not exposed** on `Board` or `DefaultBoard`.

Relying on `NaiveChess` or `chariot.internal` in one_d4 would be fragile (internal API, can change without notice). The plan below avoids that.

---

## 2. Why Use Chariot in Detectors?

1. **Single source of truth for position** — Replay and motif logic both use the same board state (Chariot), instead of “replayer produces FEN → detectors re-parse FEN into int[][].”
2. **Accurate last move** — `historyMove()` gives UCI (from/to, promotion), so we know exactly which piece moved and where, for discovered attacks and check attribution.
3. **Phase 9 check attribution** — To implement “promotion with check” (promoted piece gives check) vs “discovered check” (another piece gives check) and “double check,” we need “which piece(s) give check.” We can compute that with our existing `BoardUtils` as long as we have the position; Chariot gives us that position and the last move unambiguously.
4. **Less duplication** — No separate FEN parsing in detectors; one conversion Chariot → int[][] (or a small adapter) where we need `BoardUtils`.

---

## 3. Plan: Leverage Chariot in Detectors

### Principle

- **Chariot** = replay and canonical position (and UCI move history).
- **one_d4 BoardUtils** = attack/check/pin logic (battle-tested; no dependency on Chariot internals).
- **Adapter** = Chariot position → `int[][]` (and optional “last move from/to”) so detectors can keep using `BoardUtils` and current motif logic.

No need to call Chariot internals; no need (for now) for Chariot to expose `isCheck()` / `checkers()`.

### Step 1: Extend the position context with Chariot state

- **Option A (recommended):** Add an optional `Board` (or `DefaultBoard`) to a new or extended context type used inside the engine/motifs pipeline. `PositionContext` today is `(moveNumber, fen, whiteToMove, lastMove)`. Either:
  - Add optional `Board board` (or `DefaultBoard defaultBoard`) to `PositionContext`, or
  - Introduce `RichPositionContext` that holds `PositionContext` plus `DefaultBoard` (and maybe last UCI move), and use it where detectors run.
- **Option B:** Keep `PositionContext` as-is and add a separate `List<DefaultBoard> boardsByPosition` (or `Map<Integer, DefaultBoard>`) passed alongside `positions` into `FeatureExtractor`/detectors. Detectors then take `(positions, boardsByPosition)` or a wrapper that carries both.

Goal: detectors can access, for each position, both the existing FEN/lastMove and the Chariot position + last UCI move.

### Step 2: Replayer produces Chariot-backed positions

- In `GameReplayer.replay()`:
  - Keep building `Board board = board.play(move)` as today.
  - For each position (including initial), either attach `board` (or `DefaultBoard.of(board)`) to the context, or append to a parallel list of boards.
  - Optionally store the last UCI move per position (e.g. from Chariot’s `board` by replaying with a wrapper that records the last `Move`/UCI, or by parsing SAN to UCI only when needed). Easiest: when building the list, keep the current `board` and the last SAN; we can add a `DefaultBoard` and last UCI when we have `DefaultBoard.of(board)` and `historyMove()` (see below).
- **Catch:** `Board` from `board.play(move)` does not expose `historyMove()`. So we either:
  - Build a **list of Boards** in order (initial + after each move), and for “position after move N” we have the Board; then use `DefaultBoard.of(board)` and derive “last move” from diffing `board` with the previous Board (which square changed), or
  - Use **DefaultBoard** in the replayer: start from `DefaultBoard.of(Board.ofStandard())`, then `defaultBoard = defaultBoard.play(san1, san2, ...)` so that `defaultBoard.historyMove()` gives UCI list; for each position we have a DefaultBoard and the last UCI is `historyMove().get(historyMove().size()-1)`.

So: change replayer to maintain a list of `DefaultBoard` (or Board + last UCI) per position, and expose that in the context or parallel list.

### Step 3: Adapter Chariot → int[][] (and last move from/to)

- Add a small **ChariotBoardAdapter** (or similar) in the engine/motifs layer that:
  - Takes `DefaultBoard` (or Board + FEN parsed to squares).
  - Builds `int[][]` in one_d4’s convention (e.g. `board[0][0] = a8`, piece codes ±1..±6) by iterating `DefaultBoard.squares().all()` or `pieces().all()` and mapping `Piece` + `Side` to our codes.
  - Optionally takes “last UCI move” string and returns `fromRow, fromCol, toRow, toCol` (or a small “LastMove” record) for use in AttackDetector and check attribution.
- Detectors that today do `PinDetector.parsePlacement(ctx.fen().split(" ")[0])` can instead call the adapter with `ctx.board()` (or the board for that index) to get `int[][]`. Fallback: if no Board is present (e.g. in tests that only pass FEN), keep using FEN parsing so existing tests don’t break.

### Step 4: Refactor detectors to use Chariot-backed position when present

- **Data path:** For each position, if a Chariot board is available, use the adapter to get `int[][]` (and last UCI); otherwise use current FEN parse and SAN `lastMove()`.
- Detectors that only need the board: **CheckDetector**, **BackRankMateDetector**, **SmotheredMateDetector**, **PromotionWithCheckDetector**, **PromotionWithCheckmateDetector**, **PinDetector**, **SkewerDetector**, **AttackDetector**, **DiscoveredAttackDetector**, etc. All can keep their current logic and only change the way they get `int[][]` (and optionally last move from/to).
- **AttackDetector** and **DiscoveredAttackDetector** already compute “vacated square” and “dest square” by comparing board before/after; with Chariot we can also use last UCI to know exactly which piece moved from where, which can simplify or validate that logic.
- No change to motif enums, schema, or API DTOs; only to how positions are produced and how detectors get board/move data.

### Step 5: Phase 9 check attribution using BoardUtils + last move

- **Who gives check:** Use existing `BoardUtils.findCheckingPiece` on the Chariot-derived `int[][]`; extend to **findCheckingPieces** (return list of all squares/pieces that attack the king) so we can support double check.
- **PROMOTION_WITH_CHECK:** After a promotion move, get the set of checking pieces; if the **promoted piece’s square** (from last UCI to-square) is in that set, then the promoted piece gives check → emit PROMOTION_WITH_CHECK. Otherwise treat as discovered check + promotion only (no PROMOTION_WITH_CHECK).
- **DISCOVERED_CHECK:** Checking piece did not move this half-move: the from-square of the last UCI move is not in the set of checker squares.
- **DOUBLE_CHECK:** `findCheckingPieces` returns 2+ pieces.
- **CHECK / CHECKMATE:** Continue to use notation (+/#) for “check vs checkmate”; optionally validate against `validMoves().isEmpty()` when Board is available for consistency.

### Step 6: Optional long-term — Chariot public API

- If Chariot later exposes e.g. `Board.isCheck()`, `Board.checkers()`, or `DefaultBoard.squaresAttacking(Square.Pos kingSquare)`, we could replace our “Chariot → int[][] + BoardUtils.findCheckingPieces” with a direct Chariot call in a single place, reducing duplication. Not required for this plan.

---

## 4. Implementation Checklist

- [ ] **1.** Extend `PositionContext` (or introduce a rich context) to carry optional `DefaultBoard` and optional last UCI move string.
- [ ] **2.** Change `GameReplayer.replay()` to build positions with Chariot `DefaultBoard` per position and last UCI (e.g. via `DefaultBoard`’s `play(san)` and `historyMove()`).
- [ ] **3.** Add **ChariotBoardAdapter**: `DefaultBoard` + optional last UCI → `int[][]` and optional `(fromRow, fromCol, toRow, toCol)`.
- [ ] **4.** Refactor detectors to obtain `int[][]` (and last move from/to) from the adapter when Chariot context is present, else from FEN/`lastMove()` as today.
- [ ] **5.** Add **BoardUtils.findCheckingPieces** (return list of attacker squares) and use it for double check and check attribution.
- [ ] **6.** Implement Phase 9 semantics: PROMOTION_WITH_CHECK (promoted piece in checkers), DISCOVERED_CHECK (checker did not move), DOUBLE_CHECK (2+ checkers), using Chariot-derived board and last UCI.
- [ ] **7.** Tests: keep existing FEN-only tests; add tests that use Chariot-backed positions and assert correct check attribution and mate subtypes.

---

## 5. Summary

| Current state | After plan |
|---------------|------------|
| Replayer uses Chariot only for SAN + replay → FEN | Replayer also exposes Chariot position (DefaultBoard) and last UCI per position |
| Detectors parse FEN → int[][] and use BoardUtils | Detectors get int[][] (and last move) from Chariot via adapter; fallback to FEN when no Board |
| Check attribution from notation only (imprecise) | Check attribution from “who gives check” (BoardUtils) + “who moved” (UCI) |
| findCheckingPiece (single) | findCheckingPieces (list) for double check and attribution |

Chariot’s **public** API is enough for this plan: we use it for position and move history, and keep using one_d4’s BoardUtils for attacks and checkers. We do **not** rely on Chariot’s internal `NaiveChess` or `squaresAttackedByPiece`.
