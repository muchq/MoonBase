#ifndef DOMAINS_GAMES_APIS_ONE_D4_V2_ANALYZE_H
#define DOMAINS_GAMES_APIS_ONE_D4_V2_ANALYZE_H

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/one_d4_motifs/occurrence.h"

namespace one_d4_v2 {

/// What one ad-hoc analysis found: the motifs that occurred, lowercased,
/// each with its occurrences. Field-for-field the wire shape /v1/analyze
/// has always returned, so a caller moving to v2 changes no parsing —
/// what changes is the detector set behind it (15 here against Java's 10).
struct Analysis {
  int num_moves = 0;
  /// Keyed by lowercase motif name; kAttack — the internal primitive the
  /// detectors build on — is never a key.
  std::map<std::string, std::vector<one_d4::MotifOccurrence>> occurrences;
};

/// Bytes, not characters: the limit bounds what was read off the wire,
/// and multibyte annotation text would slip past a length check.
/// /v1/analyze's value, kept — the two endpoints answer the same clients.
inline constexpr int kMaxPgnBytes = 256 * 1024;

/// Plies, after parsing. The byte cap bounds the input; this bounds the
/// work, deterministically, where the Java endpoint reached for a wall
/// clock: detector cost tracks ply count, and the longest recorded
/// serious games sit under 300 moves — 4096 plies is not a chess game,
/// it is a lever on an unauthenticated CPU endpoint.
inline constexpr int kMaxPlies = 4096;

/// InvalidArgument for a PGN that is missing, oversized, unparseable, or
/// longer than any chess game; the analysis otherwise.
absl::StatusOr<Analysis> Analyze(std::string_view pgn);

}  // namespace one_d4_v2

#endif  // DOMAINS_GAMES_APIS_ONE_D4_V2_ANALYZE_H
