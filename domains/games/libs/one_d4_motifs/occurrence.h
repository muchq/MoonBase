#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_OCCURRENCE_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_OCCURRENCE_H

#include <optional>
#include <string>
#include <string_view>

#include "domains/games/libs/chess_cpp/side.h"
#include "domains/games/libs/one_d4_motifs/motif.h"

namespace one_d4 {

/// What a pinned piece is pinned to: its king, or a more valuable piece.
enum class PinType { kAbsolute, kRelative };

std::string_view ToString(PinType pin_type);

/// One motif firing at one ply: a row of `motif_occurrences`, field for
/// field. A field here with no column is a finding that vanishes on insert.
struct MotifOccurrence {
  Motif motif = Motif::kAttack;

  /// Half-moves from 1. White odd, Black even.
  int ply = 0;

  /// PGN full-move number; both colors of a move share it.
  int move_number = 0;

  /// Who moved, not who is next.
  chess_cpp::Side side = chess_cpp::Side::kWhite;

  std::string description;

  /// Letter + from + to ("Nf3g5"), when the mover is not the attacker.
  std::optional<std::string> moved_piece;

  /// Letter + square ("Qh5"), lowercase for Black.
  std::optional<std::string> attacker;
  std::optional<std::string> target;

  /// The move uncovered the attacker rather than being it.
  bool is_discovered = false;
  bool is_mate = false;

  /// PIN occurrences only.
  std::optional<PinType> pin_type;
};

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_OCCURRENCE_H
