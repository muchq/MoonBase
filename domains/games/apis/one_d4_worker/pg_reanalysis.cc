#include "domains/games/apis/one_d4_worker/pg_reanalysis.h"

#include <algorithm>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/occurrence_writer.h"

namespace one_d4_worker {
namespace {

// Keyset, not OFFSET. game_url is UNIQUE, so its index serves both the
// range and the order, and a row inserted behind the cursor cannot shift
// the window the way an offset's would.
//
// An empty cursor starts at the beginning without a second statement:
// every url sorts after the empty string.
constexpr char kPageAfter[] = R"sql(
SELECT game_url, pgn FROM game_features
WHERE game_url > $1
ORDER BY game_url ASC
LIMIT $2::int
)sql";

// PgGameSink's fence, one table over.
constexpr char kFence[] = R"sql(
SELECT id FROM reanalysis_requests
WHERE id = $1::uuid AND owner_id = $2
  AND status IN ('PENDING', 'PROCESSING')
  AND lease_expires_at > NOW()
FOR UPDATE
)sql";

}  // namespace

absl::StatusOr<std::vector<StoredGame>> PgGameCorpus::After(std::string_view after, int limit) {
  const absl::StatusOr<pg::Result> page =
      client_.Exec(kPageAfter, {std::string(after), std::to_string(limit)});
  if (!page.ok()) return page.status();

  std::vector<StoredGame> games;
  games.reserve(page->rows());
  for (int row = 0; row < page->rows(); ++row) {
    StoredGame game;
    game.url = page->Get(row, 0).value_or("");
    // A NULL pgn comes back empty and fails to parse, which is the same
    // outcome the Java pass gave it: counted failed, occurrences cleared.
    game.pgn = page->Get(row, 1).value_or("");
    games.push_back(std::move(game));
  }
  return games;
}

absl::Status PgOccurrenceSink::Replace(const std::vector<ReanalyzedGame>& games) {
  if (games.empty()) return absl::OkStatus();

  // Sorted here rather than trusted from the caller: ReplaceOccurrences
  // locks each game row, and the indexer's flush takes those same locks in
  // url order. An inverted pair deadlocks the two writers against each
  // other.
  std::vector<const ReanalyzedGame*> ordered;
  ordered.reserve(games.size());
  for (const ReanalyzedGame& game : games) ordered.push_back(&game);
  std::sort(ordered.begin(), ordered.end(),
            [](const ReanalyzedGame* a, const ReanalyzedGame* b) { return a->url < b->url; });

  bool still_owner = false;
  const absl::Status written = client_.InTransaction([&](pg::Transaction& tx) -> absl::Status {
    const absl::StatusOr<pg::Result> owner = tx.Exec(kFence, {request_id_, owner_});
    if (!owner.ok()) return owner.status();
    if (owner->rows() == 0) return absl::OkStatus();
    still_owner = true;

    for (const ReanalyzedGame* game : ordered) {
      const absl::Status replaced = ReplaceOccurrences(tx, game->url, game->occurrences);
      if (!replaced.ok()) return replaced;
    }
    return absl::OkStatus();
  });

  if (!written.ok()) return written;
  if (!still_owner) {
    return absl::FailedPreconditionError(
        absl::StrCat("reanalysis request ", request_id_, " is no longer held by ", owner_));
  }
  return absl::OkStatus();
}

ReanalysisEnds NewOwnedReanalysisEnds(const std::string& db_url, const std::string& request_id,
                                      const std::string& owner) {
  ReanalysisEnds ends;
  ends.client = std::make_unique<pg::Client>(db_url);
  ends.corpus = std::make_unique<PgGameCorpus>(*ends.client);
  ends.sink = std::make_unique<PgOccurrenceSink>(*ends.client, request_id, owner);
  return ends;
}

}  // namespace one_d4_worker
