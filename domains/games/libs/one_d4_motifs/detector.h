#ifndef DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_DETECTOR_H
#define DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_DETECTOR_H

#include <optional>
#include <string>
#include <vector>

#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/one_d4_motifs/motif.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4 {

/// What a detector found. Where it happened is not its business.
struct Finding {
  std::string description;
  std::optional<std::string> moved_piece;
  std::optional<std::string> attacker;
  std::optional<std::string> target;
  bool is_discovered = false;
  bool is_mate = false;
  std::optional<PinType> pin_type;
};

/// Findings go here, and this is the only place that derives ply, move
/// number and side. The Java pipeline had six copies of that arithmetic,
/// all of them wrong for Black — two plies early, and its first move
/// dropped. Porting is what found it; MotifOccurrence.plyOf is the one
/// definition it has now.
class Findings {
 public:
  /// `out` outlives this.
  Findings(Motif motif, std::vector<MotifOccurrence>* out) : motif_(motif), out_(out) {}

  /// Records `finding` as caused by the move that produced `at`.
  void Add(const chess_cpp::Position& at, Finding finding);

  Motif motif() const { return motif_; }

 private:
  Motif motif_;
  std::vector<MotifOccurrence>* out_;
};

/// One pattern, looked for during a single forward replay — no FEN is
/// written and none is parsed, which is the structural change from the
/// Java pipeline's ten detectors each re-parsing every ply.
///
/// Stateless, and has to be: Extract's span overload exists so a batch
/// worker can build the set once and run it over every game, and there is
/// no hook that would tell a detector a new game had started.
class Detector {
 public:
  virtual ~Detector() = default;

  virtual Motif motif() const = 0;

  /// Once per position after a move, in order. Never the starting position.
  virtual void OnPosition(const chess_cpp::Position& position, Findings& out) {}

  /// Once, with the final position, for patterns that are only ever how a
  /// game ended. Not called for a game with no moves.
  virtual void OnGameEnd(const chess_cpp::Position& final_position, Findings& out) {}
};

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_MOTIFS_DETECTOR_H
