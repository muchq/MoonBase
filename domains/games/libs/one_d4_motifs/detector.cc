#include "domains/games/libs/one_d4_motifs/detector.h"

#include <utility>

#include "domains/games/libs/chess_cpp/side.h"

namespace one_d4 {

void Findings::Add(const chess_cpp::Position& at, Finding finding) {
  MotifOccurrence occurrence;
  occurrence.motif = motif_;
  occurrence.move_number = at.move_number;
  // Who moved, not who is next.
  occurrence.side = chess_cpp::Opponent(at.side_to_move);
  // From the move number and the color, not from the replay's own count of
  // half-moves: a game that starts from a [FEN] tag has played fewer than
  // its move numbers say, and a ply whose parity disagrees with its color
  // breaks the read path, which joins same-side sequences on ply + 2.
  occurrence.ply = 2 * at.move_number - (occurrence.side == chess_cpp::Side::kWhite ? 1 : 0);
  occurrence.description = std::move(finding.description);
  occurrence.moved_piece = std::move(finding.moved_piece);
  occurrence.attacker = std::move(finding.attacker);
  occurrence.target = std::move(finding.target);
  occurrence.is_discovered = finding.is_discovered;
  occurrence.is_mate = finding.is_mate;
  occurrence.pin_type = finding.pin_type;
  out_->push_back(std::move(occurrence));
}

}  // namespace one_d4
