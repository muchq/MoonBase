#include "domains/games/apis/one_d4_worker/pg_title_store.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace one_d4_worker {
namespace {

constexpr char kSource[] = "roster";

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

absl::Status PgTitleStore::Save(std::string_view platform, const TitleMap& titles,
                                absl::Time observed_at) {
  if (titles.empty()) return absl::OkStatus();

  // $1 and $2 are the same for every row in the statement.
  const std::string seconds = absl::StrCat(absl::ToUnixSeconds(observed_at));

  auto it = titles.begin();
  while (it != titles.end()) {
    std::vector<std::string> params = {seconds, kSource};
    std::string values;
    for (int row = 0; row < kBatchRows && it != titles.end(); ++row, ++it) {
      // A title is never stored empty: absence of one is every untitled
      // player too, so a blank row would shadow a real title rather than
      // record that the player holds none.
      if (it->second.empty()) continue;
      const int base = static_cast<int>(params.size());
      params.push_back(std::string(platform));
      params.push_back(it->first);
      params.push_back(it->second);
      absl::StrAppend(&values, values.empty() ? "" : ",", "($", base + 1, ",$", base + 2, ",$",
                      base + 3, ",to_timestamp($1::bigint) AT TIME ZONE 'UTC',$2)");
    }
    if (values.empty()) continue;

    const absl::StatusOr<pg::Result> written = client_.Exec(
        absl::StrCat("INSERT INTO player_titles (platform, username, title, observed_at, source)"
                     " VALUES ",
                     values, kConflict),
        params);
    if (!written.ok()) return written.status();
  }
  return absl::OkStatus();
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
