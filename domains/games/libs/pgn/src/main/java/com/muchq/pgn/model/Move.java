package com.muchq.pgn.model;

import java.util.List;
import java.util.Optional;

/**
 * Represents a single move in PGN notation with optional annotations.
 *
 * @param san The Standard Algebraic Notation for the move (e.g., "e4", "Nf3", "O-O")
 * @param comment Optional comment in curly braces
 * @param nags Numeric Annotation Glyphs ($1, $2, etc.)
 * @param variations Alternative lines (recursive)
 */
public record Move(
    String san, Optional<String> comment, List<Nag> nags, List<List<Move>> variations) {
  public Move(String san) {
    this(san, Optional.empty(), List.of(), List.of());
  }

  public Move(String san, String comment) {
    this(san, Optional.of(comment), List.of(), List.of());
  }
}
