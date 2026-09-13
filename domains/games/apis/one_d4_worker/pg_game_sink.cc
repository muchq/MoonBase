#include "domains/games/apis/one_d4_worker/pg_game_sink.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/occurrence_writer.h"
#include "domains/games/apis/one_d4_worker/pg_title_store.h"
#include "domains/games/libs/chess_cpp/side.h"
#include "domains/games/libs/one_d4_motifs/motif.h"

namespace one_d4_worker {
namespace {

// The fence, as the Java flush spells it. SELECT ... FOR UPDATE rather
// than a bare read: nothing sets an isolation level, so this runs READ
// COMMITTED, where a plain SELECT is a snapshot and not a claim. A rival
// claim could commit between it and the writes below, and this
// transaction would then commit rows against a request it no longer owns.
// The lock makes that claim block until this commits or rolls back.
constexpr char kFence[] = R"sql(
SELECT id FROM indexing_requests
WHERE id = $1::uuid AND owner_id = $2
  AND status IN ('PENDING', 'PROCESSING')
  AND lease_expires_at > NOW()
FOR UPDATE
)sql";

// Column list and conflict clause from GameFeatureDao.INSERT_GAME_FEATURE.
// The empty string stands in for SQL NULL, because pg::Client binds text
// parameters and has no null. No indexed column is legitimately empty.
constexpr char kUpsertGame[] = R"sql(
INSERT INTO game_features (
    request_id, game_url, platform, white_username, black_username,
    white_elo, black_elo, white_title, black_title, time_class, eco,
    opening_name, opening_family, result, played_at, num_moves,
    indexed_at, pgn
) VALUES (
    $1::uuid, $2, $3, NULLIF($4, ''), NULLIF($5, ''),
    $6::int, $7::int, NULLIF($8, ''), NULLIF($9, ''),
    NULLIF($10, ''), NULLIF($11, ''), NULLIF($12, ''), NULLIF($13, ''),
    NULLIF($14, ''),
    CASE WHEN $15 = '' THEN NULL
         ELSE to_timestamp($15::bigint) AT TIME ZONE 'UTC' END,
    $16::int,
    to_timestamp($17::bigint) AT TIME ZONE 'UTC', $18
)
ON CONFLICT (game_url) DO UPDATE SET
    indexed_at = EXCLUDED.indexed_at,
    request_id = EXCLUDED.request_id,
    white_title = EXCLUDED.white_title,
    black_title = EXCLUDED.black_title,
    opening_name = EXCLUDED.opening_name,
    opening_family = EXCLUDED.opening_family
RETURNING id
)sql";

// IndexedPeriodDao.UPSERT_INDEXED_PERIOD, key for key. The conflict target
// is the four-column unique constraint, so a period for the same month with
// and without bullet games are separate rows rather than one overwriting
// the other.
constexpr char kUpsertPeriod[] = R"sql(
INSERT INTO indexed_periods
    (player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet)
VALUES ($1, $2, $3, to_timestamp($4::bigint) AT TIME ZONE 'UTC', $5::boolean, $6::int,
        $7::boolean)
ON CONFLICT (player, platform, year_month, exclude_bullet)
DO UPDATE SET fetched_at = EXCLUDED.fetched_at, is_complete = EXCLUDED.is_complete,
              games_count = EXCLUDED.games_count
RETURNING id
)sql";

// IndexedPeriodDao.FIND_COMPLETE. Keyed by the filter as well as the
// month, because a range indexed without bullet games answers nothing
// about the same month indexed with them.
constexpr char kFindCompletePeriod[] = R"sql(
SELECT games_count FROM indexed_periods
WHERE player = $1 AND platform = $2 AND year_month = $3
  AND exclude_bullet = $4::boolean AND is_complete = true
)sql";

std::string Number(int value) { return std::to_string(value); }

/// An absent timestamp is NULL, not the epoch.
///
/// The one place this worker does not follow the Java worker's handling of
/// a missing field. Java stores 1970-01-01, which lands the game in a
/// month it was not played in and changes what a date filter returns;
/// absent is the honest answer. It is not invisible to date filters: a
/// positive one excludes it and a negated one returns it, which is what
/// ChessQL does with every unset field (#1302).
/// The integer columns take Java's value, since 0 there is only ever a
/// rating or a move count nobody can read anything into.
std::string OptionalNumber(int64_t value) { return value == 0 ? "" : std::to_string(value); }

std::string Bool(bool value) { return value ? "true" : "false"; }

/// What a title read off the game's own PGN is filed as, so a roster
/// observation and a stated one stay distinguishable in the table.
constexpr char kStatedSource[] = "pgn";

}  // namespace

std::map<std::string, std::vector<TitleObservation>> TitleObservationsOf(
    absl::Span<const IndexedGame* const> games) {
  // Keyed by (platform, username) so the newest wins and the result is
  // already in the order the rows will be locked.
  std::map<std::string, std::map<std::string, TitleObservation>> newest;
  for (const IndexedGame* game : games) {
    for (const TitleObservation& stated : game->stated_titles) {
      auto& slot = newest[game->platform];
      const auto found = slot.find(stated.username);
      if (found == slot.end() || found->second.observed_at < stated.observed_at) {
        slot[stated.username] = stated;
      }
    }
  }

  std::map<std::string, std::vector<TitleObservation>> collected;
  for (const auto& [platform, by_username] : newest) {
    std::vector<TitleObservation>& out = collected[platform];
    out.reserve(by_username.size());
    for (const auto& [username, observation] : by_username) out.push_back(observation);
  }
  return collected;
}

absl::Status PgGameSink::Write(absl::Span<const IndexedGame> games) {
  if (games.empty()) return absl::OkStatus();

  std::vector<const IndexedGame*> ordered;
  ordered.reserve(games.size());
  for (const IndexedGame& game : games) ordered.push_back(&game);
  std::sort(ordered.begin(), ordered.end(),
            [](const IndexedGame* a, const IndexedGame* b) { return a->url < b->url; });

  bool still_owner = false;
  const absl::Status written = client_.InTransaction([&](pg::Transaction& tx) -> absl::Status {
    const absl::StatusOr<pg::Result> owner = tx.Exec(kFence, {request_id_, owner_});
    if (!owner.ok()) return owner.status();
    if (owner->rows() == 0) return absl::OkStatus();
    still_owner = true;

    for (const IndexedGame* game : ordered) {
      const absl::StatusOr<pg::Result> upserted = tx.Exec(
          kUpsertGame,
          {request_id_, game->url, game->platform, game->white_username, game->black_username,
           Number(game->white_elo), Number(game->black_elo), game->white_title, game->black_title,
           game->time_class, game->eco, game->opening_name, game->opening_family, game->result,
           OptionalNumber(game->played_at), Number(game->num_moves),
           std::to_string(game->indexed_at), game->pgn});
      if (!upserted.ok()) return upserted.status();
    }

    // Still in url order: ReplaceOccurrences locks each game row, and the
    // upsert above already holds an index-tuple lock per game_url.
    for (const IndexedGame* game : ordered) {
      const absl::Status replaced = ReplaceOccurrences(tx, game->url, game->occurrences);
      if (!replaced.ok()) return replaced;
    }

    // In this transaction rather than after it: the observation and the row
    // it was read from land together or not at all. Only titles a game
    // stated itself get here — a roster-derived one describes now, not the
    // date this game was played. Collected and key-ordered rather than sent
    // per game, so two flushes cannot take player_titles rows in opposite
    // orders; see TitleObservationsOf.
    for (const auto& [platform, observations] : TitleObservationsOf(ordered)) {
      const absl::Status observed = UpsertTitles(tx, platform, observations, kStatedSource);
      if (!observed.ok()) return observed;
    }
    return absl::OkStatus();
  });

  if (!written.ok()) return written;
  if (!still_owner) {
    return absl::FailedPreconditionError(
        absl::StrCat("request ", request_id_, " no longer names ", owner_));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::optional<int>> PgGameSink::MonthAlreadyIndexed(const IndexedMonth& month) {
  const absl::StatusOr<pg::Result> found = client_.Exec(
      kFindCompletePeriod, {month.player, month.platform, month.month, Bool(month.exclude_bullet)});
  if (!found.ok()) return found.status();
  if (found->rows() == 0) return std::nullopt;
  const std::optional<std::string> count = found->Get(0, 0);
  if (!count.has_value()) return std::nullopt;
  // stoi throws, and nothing here catches; a malformed row is a status.
  int games = 0;
  if (!absl::SimpleAtoi(*count, &games)) {
    return absl::InternalError("indexed_periods.games_count is not an integer");
  }
  return games;
}

absl::Status PgGameSink::RecordMonth(const IndexedMonth& month) {
  bool still_owner = false;
  const absl::Status written = client_.InTransaction([&](pg::Transaction& tx) -> absl::Status {
    // In the transaction that writes, not before it. The upsert below
    // carries no request to be refused by, so a check that commits
    // separately leaves a window a takeover fits inside — and what lands
    // in it is a period claiming a month holds the games this run found
    // rather than the ones its replacement did.
    const absl::StatusOr<pg::Result> owner = tx.Exec(kFence, {request_id_, owner_});
    if (!owner.ok()) return owner.status();
    if (owner->rows() == 0) return absl::OkStatus();
    still_owner = true;

    const absl::StatusOr<pg::Result> upserted = tx.Exec(
        kUpsertPeriod, {month.player, month.platform, month.month, std::to_string(month.fetched_at),
                        Bool(month.complete), Number(month.games), Bool(month.exclude_bullet)});
    return upserted.status();
  });

  if (!written.ok()) return written;
  if (!still_owner) {
    return absl::FailedPreconditionError(
        absl::StrCat("request ", request_id_, " no longer names ", owner_));
  }
  return absl::OkStatus();
}

namespace {

/// PgGameSink plus the connection it writes over.
class OwnedPgGameSink : public GameSink {
 public:
  OwnedPgGameSink(const std::string& db_url, std::string request_id, std::string owner)
      : client_(db_url), sink_(client_, std::move(request_id), std::move(owner)) {}

  absl::Status Write(absl::Span<const IndexedGame> games) override { return sink_.Write(games); }
  absl::StatusOr<std::optional<int>> MonthAlreadyIndexed(const IndexedMonth& month) override {
    return sink_.MonthAlreadyIndexed(month);
  }
  absl::Status RecordMonth(const IndexedMonth& month) override { return sink_.RecordMonth(month); }

 private:
  pg::Client client_;
  PgGameSink sink_;
};

}  // namespace

std::unique_ptr<GameSink> NewOwnedPgGameSink(const std::string& db_url, std::string request_id,
                                             std::string owner) {
  return std::make_unique<OwnedPgGameSink>(db_url, std::move(request_id), std::move(owner));
}

}  // namespace one_d4_worker
