#include "domains/games/apis/games_hub/chess_archive.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/chess_results.h"
#include "domains/games/libs/chess_play/pgn.h"
#include "domains/games/libs/chess_play/table.h"

namespace games_hub {

moonbase::games::ChessGameSummary ChessSummaryOf(const HubStore::ChessGameRow& row) {
  const chess_play::GameState& game = row.game;
  moonbase::games::ChessGameSummary summary;
  summary.archiveId = row.archive_id;
  summary.gameId = row.game_id;
  summary.ordinal = row.ordinal;
  summary.white = game.players().at(game.seatOf(chess_play::Color::kWhite));
  summary.black = game.players().at(game.seatOf(chess_play::Color::kBlack));
  summary.result = WireChessResult(chess_play::ScoreOf(game));
  summary.setupId = game.setupId();
  summary.setupName =
      std::string(chess_play::ChessSetupName(game.setupId()).value_or(game.setupId()));
  summary.plies = static_cast<int>(game.moves().size());
  summary.endedAtMs = row.ended_at_ms;
  summary.published = row.published;
  return summary;
}

std::string ChessPgnOf(int64_t archive_id, const chess_play::GameState& game, int64_t ended_at_ms) {
  return chess_play::ToPgn(
      game, {"muchq.com chess", absl::StrCat("https://muchq.com/games/chess/", archive_id), "-",
             ended_at_ms});
}

moonbase::games::ChessReview ChessReviewOf(const HubStore::ChessGameRow& row) {
  chess_play::Replayed replayed = chess_play::ReplayGame(row.game);
  moonbase::games::ChessReview review;
  review.summary = ChessSummaryOf(row);
  review.moves = row.game.moves();
  review.san = std::move(replayed.san);
  review.fens = std::move(replayed.fens);
  review.pgn = ChessPgnOf(row.archive_id, row.game, row.ended_at_ms);
  return review;
}

}  // namespace games_hub
