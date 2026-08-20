#include "domains/games/apis/one_d4_worker/reanalysis_run.h"

#include <utility>

#include "absl/status/status.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/one_d4_motifs/extract.h"

namespace one_d4_worker {

ReanalysisRun::ReanalysisRun(GameCorpus& corpus, OccurrenceSink& sink, Options options)
    : corpus_(corpus), sink_(sink), options_(std::move(options)) {}

absl::StatusOr<ReanalysisReport> ReanalysisRun::Execute(const ReanalysisJob& job,
                                                        ReanalysisLease& lease) {
  ReanalysisReport report;
  // Resume, not restart. The counts come back with the cursor so a taken-over
  // pass reports the whole corpus it covered rather than only its own share.
  report.cursor = job.cursor_game_url;
  report.games_processed = job.games_processed;
  report.games_failed = job.games_failed;

  while (true) {
    if (options_.stopping && options_.stopping()) {
      report.stopped = Stopped::kShutdown;
      return report;
    }
    if (lease.OutOfTime()) {
      report.stopped = Stopped::kRunCeiling;
      return report;
    }
    if (!lease.Keep()) {
      report.lease_lost = true;
      return report;
    }

    const absl::StatusOr<std::vector<StoredGame>> page =
        corpus_.After(report.cursor, options_.batch_size);
    if (!page.ok()) return page.status();
    if (page->empty()) return report;

    std::vector<ReanalyzedGame> reanalyzed;
    reanalyzed.reserve(page->size());
    int failed_here = 0;
    for (const StoredGame& game : *page) {
      // A game that will not replay is one game, not a failed pass — the
      // same call the indexer makes on the same stored PGN. It still gets
      // an entry, with no occurrences: a game whose second look found
      // nothing must lose the rows it had, and skipping it here would
      // leave those rows behind forever.
      const absl::StatusOr<chess_cpp::ParsedGame> parsed = chess_cpp::ParseGame(game.pgn);
      const absl::StatusOr<one_d4::GameFeatures> features =
          parsed.ok() ? one_d4::Extract(*parsed)
                      : absl::StatusOr<one_d4::GameFeatures>(parsed.status());

      ReanalyzedGame row;
      row.url = game.url;
      if (features.ok()) {
        row.occurrences = features->occurrences;
      } else {
        ++failed_here;
      }
      reanalyzed.push_back(std::move(row));
    }

    const absl::Status written = sink_.Replace(reanalyzed);
    // FailedPrecondition is the fence saying no: the claim went somewhere
    // else, which is an outcome and not an error — mapped as IndexRun maps
    // it, because a Fail here would mark FAILED a request somebody else is
    // now running.
    if (absl::IsFailedPrecondition(written)) {
      report.lease_lost = true;
      return report;
    }
    if (!written.ok()) return written;

    // Cursor first, then the counts that go with it — one call, because the
    // two disagreeing is what a resume cannot recover from.
    report.cursor = page->back().url;
    report.games_processed += static_cast<int>(page->size()) - failed_here;
    report.games_failed += failed_here;

    if (!lease.Report(report.cursor, report.games_processed, report.games_failed)) {
      report.lease_lost = true;
      return report;
    }

    // A short page is the end of the corpus. Asking again would cost a
    // round trip to be told the same thing.
    if (static_cast<int>(page->size()) < options_.batch_size) return report;
  }
}

}  // namespace one_d4_worker
