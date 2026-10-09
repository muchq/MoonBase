#include "domains/games/apis/games_hub/chess_archive.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/chess_results.h"
#include "domains/games/libs/chess_play/pgn.h"
#include "domains/games/libs/chess_play/table.h"

namespace games_hub {

namespace {

// What a game says of itself, wherever it was archived.
moonbase::games::ChessGameSummary SummaryOf(int64_t archive_id, const chess_play::GameState& game,
                                            int64_t ended_at_ms, bool published) {
  moonbase::games::ChessGameSummary summary;
  summary.archiveId = archive_id;
  summary.white = game.players().at(game.seatOf(chess_play::Color::kWhite));
  summary.black = game.players().at(game.seatOf(chess_play::Color::kBlack));
  summary.result = WireChessResult(chess_play::ScoreOf(game));
  summary.setupId = game.setupId();
  summary.setupName =
      std::string(chess_play::ChessSetupName(game.setupId()).value_or(game.setupId()));
  summary.plies = static_cast<int>(game.moves().size());
  summary.endedAtMs = ended_at_ms;
  summary.published = published;
  return summary;
}

moonbase::games::ChessReview ReviewOf(moonbase::games::ChessGameSummary summary,
                                      const chess_play::GameState& game) {
  chess_play::Replayed replayed = chess_play::ReplayGame(game);
  moonbase::games::ChessReview review;
  review.pgn = ChessPgnOf(summary.archiveId, game, summary.endedAtMs);
  review.summary = std::move(summary);
  review.moves = game.moves();
  review.san = std::move(replayed.san);
  review.fens = std::move(replayed.fens);
  return review;
}

}  // namespace

moonbase::games::ChessGameSummary ChessSummaryOf(const HubStore::ChessGameRow& row) {
  moonbase::games::ChessGameSummary summary =
      SummaryOf(row.archive_id, row.game, row.ended_at_ms, row.published);
  summary.gameId = row.game_id;
  summary.ordinal = row.ordinal;
  return summary;
}

std::string ChessPgnOf(int64_t archive_id, const chess_play::GameState& game, int64_t ended_at_ms) {
  return chess_play::ToPgn(
      game, {"muchq.com chess", absl::StrCat("https://muchq.com/games/chess/", archive_id), "-",
             ended_at_ms});
}

moonbase::games::ChessReview ChessReviewOf(const HubStore::ChessGameRow& row) {
  return ReviewOf(ChessSummaryOf(row), row.game);
}

moonbase::games::ChessReview ChessReviewOf(const HubStore::PublishedChessGame& game) {
  return ReviewOf(SummaryOf(game.archive_id, game.game, game.ended_at_ms, true), game.game);
}

}  // namespace games_hub
