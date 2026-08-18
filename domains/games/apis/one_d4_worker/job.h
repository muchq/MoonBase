#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_JOB_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_JOB_H

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace one_d4_worker {

/// A month of a player's archive, as `indexing_requests` spells it.
struct YearMonth {
  int year = 0;
  unsigned month = 0;

  static absl::StatusOr<YearMonth> Parse(std::string_view text);

  std::string ToString() const;
  YearMonth Next() const;

  friend bool operator==(YearMonth a, YearMonth b) {
    return a.year == b.year && a.month == b.month;
  }
  friend bool operator<(YearMonth a, YearMonth b) {
    return a.year != b.year ? a.year < b.year : a.month < b.month;
  }
};

/// One claimed row of `indexing_requests`.
struct IndexJob {
  std::string id;
  std::string player;
  std::string platform;
  std::string start_month;
  std::string end_month;
  bool exclude_bullet = false;
  bool skip_cache = false;
  int attempts = 0;

  /// The months this job covers, oldest first.
  static absl::StatusOr<std::vector<YearMonth>> Months(std::string_view start,
                                                       std::string_view end);
  absl::StatusOr<std::vector<YearMonth>> Months() const;
};

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_JOB_H
