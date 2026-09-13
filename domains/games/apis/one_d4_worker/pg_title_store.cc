#include "domains/games/apis/one_d4_worker/pg_title_store.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace one_d4_worker {
namespace {

// Ordered, not last-write-wins. Indexing is not chronological — a backfill
// of an old month runs after a recent one is indexed — so the guard is what
// stops an older observation overwriting a newer title with a stale one.
// A row whose observation is not newer is left exactly as it was.
constexpr char kConflict[] = R"sql(
ON CONFLICT (platform, username) DO UPDATE SET
    title = EXCLUDED.title,
    observed_at = EXCLUDED.observed_at,
    source = EXCLUDED.source
WHERE player_titles.observed_at < EXCLUDED.observed_at
)sql";

}  // namespace

constexpr char kRosterSource[] = "roster";

absl::Status UpsertTitles(pg::Transaction& tx, std::string_view platform,
                          absl::Span<const TitleObservation> observations,
                          std::string_view source) {
  auto it = observations.begin();
  while (it != observations.end()) {
    std::vector<std::string> params = {std::string(source)};
    std::string values;
    for (int row = 0; row < PgTitleStore::kBatchRows && it != observations.end(); ++row, ++it) {
      // A title is never stored empty: absence of one is every untitled
      // player too, so a blank row would shadow a real title rather than
      // record that the player holds none.
      if (it->title.empty()) continue;
      const int base = static_cast<int>(params.size());
      params.push_back(std::string(platform));
      params.push_back(it->username);
      params.push_back(it->title);
      params.push_back(absl::StrCat(it->observed_at));
      absl::StrAppend(&values, values.empty() ? "" : ",", "($", base + 1, ",$", base + 2, ",$",
                      base + 3, ",to_timestamp($", base + 4, "::bigint) AT TIME ZONE 'UTC',$1)");
    }
    if (values.empty()) continue;

    const absl::StatusOr<pg::Result> written = tx.Exec(
        absl::StrCat("INSERT INTO player_titles (platform, username, title, observed_at, source)"
                     " VALUES ",
                     values, kConflict),
        params);
    if (!written.ok()) return written.status();
  }
  return absl::OkStatus();
}

absl::Status PgTitleStore::Save(std::string_view platform, const TitleMap& titles,
                                absl::Time observed_at) {
  if (titles.empty()) return absl::OkStatus();

  // The roster speaks for one moment, so every pair carries the same stamp.
  const int64_t seconds = absl::ToUnixSeconds(observed_at);
  std::vector<TitleObservation> observations;
  observations.reserve(titles.size());
  for (const auto& [username, title] : titles) {
    observations.push_back({username, title, seconds});
  }

  // All batches or none. AdoptStored takes any non-empty table as a complete
  // roster, so a half-written one answers for the players it is missing.
  return client_.InTransaction([&](pg::Transaction& tx) -> absl::Status {
    return UpsertTitles(tx, platform, observations, kRosterSource);
  });
}

absl::StatusOr<TitleMap> PgTitleStore::Load(std::string_view platform) {
  const absl::StatusOr<pg::Result> rows = client_.Exec(
      "SELECT username, title FROM player_titles WHERE platform = $1", {std::string(platform)});
  if (!rows.ok()) return rows.status();

  TitleMap titles;
  for (int row = 0; row < rows->rows(); ++row) {
    const std::optional<std::string> username = rows->Get(row, 0);
    const std::optional<std::string> title = rows->Get(row, 1);
    if (!username.has_value() || !title.has_value() || title->empty()) continue;
    titles.insert_or_assign(*username, *title);
  }
  return titles;
}

}  // namespace one_d4_worker
