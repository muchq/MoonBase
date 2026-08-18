#include "domains/games/libs/one_d4_motifs/extract.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "chess.hpp"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/chess_cpp/replay.h"
#include "domains/games/libs/one_d4_motifs/detectors.h"

namespace one_d4 {
namespace {

/// The last position, kept alive past the replay.
///
/// chess_cpp::Position is a borrowed view: its boards belong to the
/// replayer and are gone the moment the pass ends. OnGameEnd runs after
/// that, so the pieces it needs are copied. Once per game, not once per
/// ply — the caller knows how many moves it handed over, so the copy is
/// taken at that ply and nowhere else.
class FinalPosition {
 public:
  void Remember(const chess_cpp::Position& position) {
    ply_ = position.ply;
    move_number_ = position.move_number;
    side_to_move_ = position.side_to_move;
    board_ = position.board;
    move_ = position.last->move;
    san_ = position.last->san;
    by_ = position.last->by;
    before_ = position.last->before;
    seen_ = true;
  }

  bool seen() const { return seen_; }

  chess_cpp::Position View() const {
    return chess_cpp::Position{ply_, move_number_, side_to_move_, board_,
                               chess_cpp::PlayedMove{move_, san_, by_, before_}};
  }

 private:
  bool seen_ = false;
  int ply_ = 0;
  int move_number_ = 0;
  chess_cpp::Side side_to_move_ = chess_cpp::Side::kWhite;
  chess::Board board_;
  chess::Board before_;
  chess::Move move_;
  std::string san_;
  chess_cpp::Side by_ = chess_cpp::Side::kWhite;
};

}  // namespace

absl::StatusOr<GameFeatures> Extract(const chess_cpp::ParsedGame& game,
                                     absl::Span<const std::unique_ptr<Detector>> detectors) {
  const absl::StatusOr<std::string_view> start = chess_cpp::StartFen(game.headers);
  if (!start.ok()) return start.status();

  GameFeatures features;
  // One bucket per detector: the output is grouped by motif, which is how
  // the rows are read back. Appending straight to features.occurrences
  // would interleave the motifs and order by ply instead.
  std::vector<std::vector<MotifOccurrence>> by_detector(detectors.size());
  FinalPosition final_position;
  const int last_ply = static_cast<int>(game.san_moves.size());

  const absl::Status replayed =
      chess_cpp::ReplayFrom(*start, game.san_moves, [&](const chess_cpp::Position& position) {
        if (!position.last.has_value()) return;  // nothing happened at the start
        features.num_moves = position.move_number;
        if (position.ply == last_ply) final_position.Remember(position);
        for (std::size_t i = 0; i < detectors.size(); ++i) {
          Findings findings(detectors[i]->motif(), &by_detector[i]);
          detectors[i]->OnPosition(position, findings);
        }
      });
  if (!replayed.ok()) return replayed;

  if (final_position.seen()) {
    const chess_cpp::Position view = final_position.View();
    for (std::size_t i = 0; i < detectors.size(); ++i) {
      Findings findings(detectors[i]->motif(), &by_detector[i]);
      detectors[i]->OnGameEnd(view, findings);
    }
  }

  for (std::vector<MotifOccurrence>& found : by_detector) {
    if (found.empty()) continue;
    features.motifs.insert(found.front().motif);
    for (MotifOccurrence& occurrence : found) {
      features.occurrences.push_back(std::move(occurrence));
    }
  }
  return features;
}

absl::StatusOr<GameFeatures> Extract(const chess_cpp::ParsedGame& game) {
  const std::vector<std::unique_ptr<Detector>> detectors = DefaultDetectors();
  return Extract(game, absl::MakeConstSpan(detectors));
}

}  // namespace one_d4
