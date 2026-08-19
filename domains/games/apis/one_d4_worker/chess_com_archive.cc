#include "domains/games/apis/one_d4_worker/chess_com_archive.h"

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "moonbase/chess_com/client.h"

namespace one_d4_worker {
namespace {

std::string Or(const std::optional<std::string>& value) { return value.value_or(""); }

int Or(const std::optional<int>& value) { return value.value_or(0); }

void CopyPlayer(const std::optional<moonbase::chess_com::PlayerResult>& player,
                std::string& username, int& rating, std::string& result) {
  if (!player.has_value()) return;
  username = Or(player->username);
  rating = Or(player->rating);
  result = Or(player->result);
}

}  // namespace

absl::StatusOr<std::vector<ArchivedGame>> ChessComArchive::FetchMonth(std::string_view player,
                                                                      YearMonth month) {
  const auto archive = client_.FetchArchive(player, month.year, month.month);
  if (!archive.ok()) {
    const std::string message =
        absl::StrCat("chess.com archive ", player, " ", month.ToString(), ": ",
                     archive.error().code(), ": ", archive.error().message());
    // Only the modeled 404 is NotFound. Everything else — rate limits, 5xx,
    // a transport that never connected — is a failure to read the month,
    // and the run must not walk past it.
    if (archive.error().code() == "ArchiveNotFound") return absl::NotFoundError(message);
    return absl::UnavailableError(message);
  }

  std::vector<ArchivedGame> games;
  games.reserve(archive->games.size());
  for (const moonbase::chess_com::PlayedGame& played : archive->games) {
    ArchivedGame game;
    game.url = Or(played.url);
    game.pgn = Or(played.pgn);
    game.time_class = Or(played.timeClass);
    CopyPlayer(played.white, game.white_username, game.white_rating, game.white_result);
    CopyPlayer(played.black, game.black_username, game.black_rating, game.black_result);
    game.eco_url = Or(played.eco);
    if (played.endTime.has_value()) {
      game.end_time = played.endTime->epoch_milliseconds() / 1000;
    }
    games.push_back(std::move(game));
  }
  return games;
}

absl::StatusOr<std::vector<std::string>> ChessComArchive::FetchTitled(std::string_view title) {
  const auto roster = client_.FetchTitled(title);
  if (roster.ok()) return roster->players;
  return absl::UnavailableError(absl::StrCat(
      "chess.com titled ", title, ": ", roster.error().code(), ": ", roster.error().message()));
}

}  // namespace one_d4_worker
