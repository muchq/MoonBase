#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_RESULT_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_RESULT_H

#include <string_view>

#include "absl/types/span.h"

namespace one_d4_worker {

/// The game's result in standard notation, from the two strings chess.com
/// reports per side: "1-0", "0-1", "1/2-1/2", or "unknown".
std::string_view ResultOf(std::string_view white, std::string_view black);

/// The vocabularies themselves, so a contract test can compare them with
/// the Java worker's in both directions. A word one side knows and the
/// other does not is two indexes disagreeing about who won, on rows a
/// query cannot tell apart.
absl::Span<const std::string_view> KnownDraws();
absl::Span<const std::string_view> KnownLosses();

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_RESULT_H
