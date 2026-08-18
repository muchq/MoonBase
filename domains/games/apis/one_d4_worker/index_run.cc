#include "domains/games/apis/one_d4_worker/index_run.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

RunObserver& IndexRun::observer() {
  static RunObserver* const nobody = new RunObserver();
  return options_.observer != nullptr ? *options_.observer : *nobody;
}

absl::Status IndexRun::Flush(std::vector<IndexedGame>& batch) {
  if (batch.empty()) return absl::OkStatus();
  const absl::Status written = sink_.Write(batch);
  if (written.ok()) batch.clear();
  return written;
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

    absl::StatusOr<std::vector<ArchivedGame>> games = archive_.FetchMonth(job.player, month);
    if (absl::IsNotFound(games.status())) {
      // A player who did not play that month is the common case.
      observer().ArchiveFetched("no_archive");
      observer().MonthFinished("empty", 0);
      continue;
    }
    if (!games.ok()) {
      observer().ArchiveFetched("error");
      return games.status();
    }
    observer().ArchiveFetched("ok");

    int month_count = 0;
    for (const ArchivedGame& game : *games) {
      if (job.exclude_bullet && game.time_class == "bullet") continue;

      const absl::StatusOr<chess_cpp::ParsedGame> parsed = chess_cpp::ParseGame(game.pgn);
      if (!parsed.ok()) continue;
      const absl::StatusOr<one_d4::GameFeatures> features = one_d4::Extract(*parsed);
      if (!features.ok()) continue;

      IndexedGame row;
      row.url = game.url;
      row.platform = job.platform;
      row.white_username = game.white_username;
      row.black_username = game.black_username;
      row.white_elo = game.white_rating;
      row.black_elo = game.black_rating;
      row.time_class = game.time_class;
      row.eco = EcoFrom(parsed->headers);
      row.opening_name = OpeningNameFromEcoUrl(game.eco_url);
      row.opening_family = OpeningFamilyFromName(row.opening_name);
      row.result = std::string(ResultOf(game.white_result, game.black_result));
      row.played_at = game.end_time;
      row.num_moves = features->num_moves;
      row.pgn = game.pgn;
      row.occurrences = features->occurrences;

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
        if (const absl::Status written = Flush(batch); !written.ok()) return written;
      }
    }

    if (const absl::Status written = Flush(batch); !written.ok()) return written;
    observer().MonthFinished("indexed", month_count);
  }

  return report;
}

}  // namespace one_d4_worker
