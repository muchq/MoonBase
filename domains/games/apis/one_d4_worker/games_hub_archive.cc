#include "domains/games/apis/one_d4_worker/games_hub_archive.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "domains/games/apis/one_d4_worker/pgn_games.h"

namespace one_d4_worker {
namespace {

// How long one read of the feed answers for.
constexpr absl::Duration kFresh = absl::Minutes(1);

// The hub's [Site] for a game: its URL, ending in its archive id.
constexpr std::string_view kSitePrefix = "https://muchq.com/games/chess/";

// The [Site] tag's value, read off the text so that a game that will not
// parse still says where it is.
std::string_view SiteOf(std::string_view block) {
  constexpr std::string_view kTag = "[Site \"";
  const std::size_t at = block.find(kTag);
  if (at == std::string_view::npos) return "";
  block.remove_prefix(at + kTag.size());
  return block.substr(0, block.find('"'));
}

std::optional<int64_t> ArchiveIdOf(std::string_view site) {
  if (!absl::ConsumePrefix(&site, kSitePrefix)) return std::nullopt;
  int64_t id = 0;
  if (!absl::SimpleAtoi(site, &id)) return std::nullopt;
  return id;
}

std::string TimeClassOf(const chess_cpp::Headers& headers) {
  const auto control = headers.Get("TimeControl");
  return control.has_value() ? TimeClassOfControl(*control) : "";
}

}  // namespace

opal::ClientConfig GamesHubArchive::DefaultClientConfig(std::string endpoint) {
  opal::ClientConfig config;
  config.endpoint = std::move(endpoint);
  config.user_agent = "MoonBase indexer/1.0";
  config.request_timeout_ms = 30'000;
  // Waits of up to 1, 2, 4 and 8s: past the hub's 15s start period.
  config.retry.max_attempts = 5;
  config.retry.initial_backoff = std::chrono::seconds(1);
  return config;
}

absl::StatusOr<std::unique_ptr<GamesHubArchive>> GamesHubArchive::Create(
    opal::ClientConfig config, std::function<absl::Time()> now) {
  auto client = moonbase::games::GamesHubClient::Create(std::move(config));
  if (!client.ok()) return absl::InvalidArgumentError(client.error().message());
  return std::unique_ptr<GamesHubArchive>(new GamesHubArchive(std::move(*client), std::move(now)));
}

absl::StatusOr<std::vector<ArchivedGame>> GamesHubArchive::ReadFeed() const {
  std::vector<ArchivedGame> games;
  moonbase::games::ExportChessGamesInput input;
  input.after = 0;
  while (true) {
    const auto page = client_.ExportChessGames(input);
    if (!page.ok()) {
      return absl::UnavailableError(absl::StrCat("games_hub feed after ", *input.after, ": ",
                                                 page.error().code(), ": ",
                                                 page.error().message()));
    }
    const std::string pgn = page->pgn.ToString();
    const std::vector<std::string_view> blocks = SplitPgnGames(pgn);
    if (blocks.empty()) {
      // Read as the end of the feed, a body that is not PGN would record
      // every month complete and empty.
      if (absl::StripAsciiWhitespace(pgn).empty()) return games;
      return absl::DataLossError(absl::StrCat("games_hub feed after ", *input.after, ": not PGN"));
    }

    int64_t last = *input.after;
    for (const std::string_view block : blocks) {
      // Every game moves the cursor, so one that does not say where it is
      // would be read again forever.
      const std::string_view site = SiteOf(block);
      const std::optional<int64_t> id = ArchiveIdOf(site);
      if (!id.has_value() || *id <= last) {
        return absl::DataLossError(absl::StrCat("games_hub feed after ", *input.after,
                                                ": no archive id past ", last, " in [Site \"", site,
                                                "\"]"));
      }
      last = *id;
      // A game that will not parse names no players, so no month keeps it,
      // and the read goes on: read whole, the feed would otherwise fail
      // every month for every player on one bad game.
      games.push_back(ArchivedGameFromPgn(block, TimeClassOf));
    }
    input.after = last;
  }
}

absl::StatusOr<std::vector<ArchivedGame>> GamesHubArchive::FetchMonth(std::string_view player,
                                                                      YearMonth month) {
  const int64_t since = month.FirstInstant();
  const int64_t until = month.Next().FirstInstant();
  const absl::MutexLock lock(mu_);
  if (now_() - read_at_ >= kFresh) {
    absl::StatusOr<std::vector<ArchivedGame>> feed = ReadFeed();
    if (!feed.ok()) return feed.status();
    feed_ = *std::move(feed);
    read_at_ = now_();
  }
  std::vector<ArchivedGame> games;
  for (const ArchivedGame& game : feed_) {
    const bool theirs = absl::EqualsIgnoreCase(game.white_username, player) ||
                        absl::EqualsIgnoreCase(game.black_username, player);
    if (theirs && game.end_time >= since && game.end_time < until) games.push_back(game);
  }
  return games;
}

std::string TimeClassOfControl(std::string_view control) {
  const std::vector<std::string_view> parts = absl::StrSplit(control, '+');
  int64_t initial = 0;
  int64_t increment = 0;
  if (parts.size() != 2 || !absl::SimpleAtoi(parts[0], &initial) ||
      !absl::SimpleAtoi(parts[1], &increment)) {
    return "";
  }
  const int64_t estimate = initial + 40 * increment;
  if (estimate < 30) return "ultrabullet";
  if (estimate < 180) return "bullet";
  if (estimate < 480) return "blitz";
  if (estimate < 1500) return "rapid";
  return "classical";
}

}  // namespace one_d4_worker
