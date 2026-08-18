#include "domains/games/apis/one_d4_worker/index_run.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/openings.h"
#include "domains/games/apis/one_d4_worker/result.h"
#include "domains/games/libs/chess_cpp/parsed_game.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/one_d4_motifs/extract.h"

namespace one_d4_worker {
namespace {

/// The ECO code lives in the PGN tag; the ECOUrl carries the name instead.
std::string EcoFrom(const chess_cpp::Headers& headers) {
  const auto eco = headers.Get("ECO");
  return eco.has_value() ? std::string(*eco) : "";
}

}  // namespace

IndexRun::IndexRun(ArchiveSource& archive, GameSink& sink, Options options)
    : archive_(archive), sink_(sink), options_(std::move(options)) {}

int64_t IndexRun::Now() const {
  return options_.now ? options_.now() : absl::ToUnixSeconds(absl::Now());
}

absl::Status IndexRun::RecordMonth(const IndexJob& job, YearMonth month, int64_t fetched_at,
                                   int games, bool nothing_degraded) {
  IndexedMonth period;
  period.player = job.player;
  period.platform = job.platform;
  period.month = month.ToString();
  period.fetched_at = fetched_at;
  period.games = games;
  // Complete only once the month itself is over, so the current month is
  // always refetched. Storing today's month complete would freeze it: the
  // Java worker's period cache honours the flag and skips the month on
  // every later request, so games played after this run are never indexed.
  period.complete = fetched_at >= month.Next().FirstInstant() && nothing_degraded;
  period.exclude_bullet = job.exclude_bullet;
  return sink_.RecordMonth(period);
}

RunObserver& IndexRun::observer() {
  static RunObserver* const nobody = new RunObserver();
  return options_.observer != nullptr ? *options_.observer : *nobody;
}

absl::Status IndexRun::Flush(std::vector<IndexedGame>& batch) {
  // No early return on an empty batch. An empty write writes nothing but
  // still asks the question, and the answer is what gates RecordMonth,
  // which carries no fence of its own. A month whose games were all
  // bullet-filtered, or whose count is an exact multiple of batch_size,
  // arrives here empty — and skipping the question there is a hole a
  // takeover fits through.
  const absl::Status written = sink_.Write(batch);
  if (written.ok()) batch.clear();
  return written;
}

std::string IndexRun::TitleOf(std::string_view player, bool& complete) {
  if (player.empty()) return "";
  const auto cached = titles_.find(player);
  if (cached != titles_.end()) return cached->second;
  if (unreachable_titles_.count(player) != 0) return "";

  const absl::StatusOr<std::string> title = archive_.FetchTitle(player);
  // A failure is not an answer. Caching one would carry a bad minute
  // across a decade of backfill; recording the month complete without it
  // would do the same to the period cache.
  if (!title.ok()) {
    unreachable_titles_.emplace(player);
    complete = false;
    return "";
  }
  titles_.emplace(player, *title);
  return *title;
}

absl::StatusOr<RunReport> IndexRun::Execute(const IndexJob& job, LeaseKeeper& lease) {
  const absl::StatusOr<std::vector<YearMonth>> months = job.Months();
  if (!months.ok()) return months.status();

  RunReport report;
  std::vector<IndexedGame> batch;

  for (const YearMonth month : *months) {
    if (options_.stopping && options_.stopping()) {
      report.stopped = Stopped::kShutdown;
      return report;
    }
    // Ownership before work: a run that has lost the range must not write
    // over whoever holds it now.
    if (!lease.Keep()) {
      report.lease_lost = true;
      return report;
    }

    // Stamped before the month's games are written. See IndexedMonth.
    const int64_t fetched_at = Now();
    absl::StatusOr<std::vector<ArchivedGame>> games = archive_.FetchMonth(job.player, month);
    if (!games.ok()) {
      observer().ArchiveFetched("error");
      return games.status();
    }
    if (games->empty()) {
      // The month was read and the answer is "none". Distinct from a 404,
      // which says the archive was not read at all. Recorded anyway: a
      // month with no period row is indistinguishable from one retention
      // swept, and the cache would miss on it forever.
      observer().ArchiveFetched("no_archive");
      // Ownership first, and asked of the sink rather than the lease: the
      // fetch above can outlive a lease, and a period row written over a
      // month a replacement already indexed properly would cache it as
      // holding zero games.
      if (const absl::Status written = Flush(batch); !written.ok()) {
        if (!absl::IsFailedPrecondition(written)) return written;
        report.lease_lost = true;
        return report;
      }
      if (const absl::Status recorded = RecordMonth(job, month, fetched_at, 0, true);
          !recorded.ok()) {
        return recorded;
      }
      observer().MonthFinished("empty", 0);
      continue;
    }
    observer().ArchiveFetched("ok");

    int month_count = 0;
    bool complete = true;
    // A failed lookup is worth retrying next month, not next game: a month
    // of four hundred games between two regulars is two profile calls, and
    // eight hundred against an API that rate-limits is how one bad minute
    // sustains itself.
    unreachable_titles_.clear();
    for (const ArchivedGame& game : *games) {
      if (job.exclude_bullet && game.time_class == "bullet") continue;

      // A game that will not replay is still a game: its players, result,
      // opening and PGN are all readable, and the Java worker records it
      // with no motifs and no moves rather than dropping it. Skipping it
      // here would leave the row missing on one indexer and present on the
      // other, and games_count disagreeing with both.
      const absl::StatusOr<chess_cpp::ParsedGame> parsed = chess_cpp::ParseGame(game.pgn);
      const absl::StatusOr<one_d4::GameFeatures> features =
          parsed.ok() ? one_d4::Extract(*parsed)
                      : absl::StatusOr<one_d4::GameFeatures>(parsed.status());

      IndexedGame row;
      row.url = game.url;
      row.platform = job.platform;
      row.white_username = game.white_username;
      row.black_username = game.black_username;
      row.white_elo = game.white_rating;
      row.black_elo = game.black_rating;
      row.white_title = TitleOf(game.white_username, complete);
      row.black_title = TitleOf(game.black_username, complete);
      row.time_class = game.time_class;
      row.eco = parsed.ok() ? EcoFrom(parsed->headers) : "";
      row.opening_name = OpeningNameFromEcoUrl(game.eco_url);
      row.opening_family = OpeningFamilyFromName(row.opening_name);
      row.result = std::string(ResultOf(game.white_result, game.black_result));
      row.played_at = game.end_time;
      row.num_moves = features.ok() ? features->num_moves : 0;
      row.pgn = game.pgn;
      if (features.ok()) row.occurrences = features->occurrences;
      row.indexed_at = Now();

      observer().GameIndexed(row);
      batch.push_back(std::move(row));
      ++month_count;
      ++report.games_indexed;

      if (static_cast<int>(batch.size()) >= options_.batch_size) {
        // Re-checked per batch, not per month: a month of four hundred
        // games outlives a lease that a stalled run stopped renewing.
        if (!lease.Keep()) {
          report.lease_lost = true;
          return report;
        }
        if (const absl::Status written = Flush(batch); !written.ok()) {
          if (!absl::IsFailedPrecondition(written)) return written;
          report.lease_lost = true;
          return report;
        }
      }
    }

    if (const absl::Status written = Flush(batch); !written.ok()) {
      if (!absl::IsFailedPrecondition(written)) return written;
      report.lease_lost = true;
      return report;
    }
    if (const absl::Status recorded = RecordMonth(job, month, fetched_at, month_count, complete);
        !recorded.ok()) {
      return recorded;
    }
    observer().MonthFinished(complete ? "indexed" : "degraded", month_count);
  }

  return report;
}

}  // namespace one_d4_worker
