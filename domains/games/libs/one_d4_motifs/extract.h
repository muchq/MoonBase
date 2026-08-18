#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_EXTRACT_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_EXTRACT_H

#include <memory>
#include <set>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "domains/games/libs/chess_cpp/parsed_game.h"
#include "domains/games/libs/one_d4_motifs/detector.h"
#include "domains/games/libs/one_d4_motifs/motif.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4 {

/// What one game turned out to contain.
struct GameFeatures {
  /// Which motifs fired at all — `motifs` in the game_features row.
  std::set<Motif> motifs;

  /// The last full-move number played.
  int num_moves = 0;

  /// Every firing, grouped by detector in DefaultDetectors() order, and in
  /// ply order within a detector. Not ply-major: a game's PIN rows all
  /// precede its CROSS_PIN rows.
  std::vector<MotifOccurrence> occurrences;
};

/// Replays `game` once and runs every detector over each position.
///
/// InvalidArgument when the game will not replay — an illegal move, or tags
/// that name no position. Indexing treats that as a game with no motifs;
/// a caller asking about one game it chose wants the error, since "nothing
/// found" and "never played" are not the same answer.
absl::StatusOr<GameFeatures> Extract(const chess_cpp::ParsedGame& game,
                                     absl::Span<const std::unique_ptr<Detector>> detectors);

/// Extract with DefaultDetectors().
absl::StatusOr<GameFeatures> Extract(const chess_cpp::ParsedGame& game);

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_EXTRACT_H
