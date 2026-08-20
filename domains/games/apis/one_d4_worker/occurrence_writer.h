#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_OCCURRENCE_WRITER_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_OCCURRENCE_WRITER_H

#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {

/// Replaces one game's motif occurrences, inside a transaction the caller
/// already opened.
///
/// Shared by the two writers that produce them — indexing a fetched archive
/// and reanalysing a stored one — because they write the same rows the same
/// way, and a second copy of this statement would drift from
/// motif_occurrences without anything noticing.
///
/// Two properties the callers depend on and cannot supply themselves:
///
/// - It locks the game row before touching occurrences. The caller must
///   present its games in url order, since the game_features upsert takes
///   an index-tuple lock per game_url and an inverted pair deadlocks.
/// - Delete and insert land in one transaction. Committed separately they
///   leave a window in which the other writer inserts its own occurrences
///   for the same game and both copies survive — the doubling
///   ConcurrentFlushTest demonstrates.
absl::Status ReplaceOccurrences(pg::Transaction& tx, std::string_view game_url,
                                absl::Span<const one_d4::MotifOccurrence> occurrences);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_OCCURRENCE_WRITER_H
