#include "domains/games/apis/one_d4_v2/analyze.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "domains/games/libs/chess_cpp/pgn.h"
#include "domains/games/libs/one_d4_motifs/extract.h"
#include "domains/games/libs/one_d4_motifs/motif.h"

namespace one_d4_v2 {

absl::StatusOr<Analysis> Analyze(std::string_view pgn) {
  if (absl::StripAsciiWhitespace(pgn).empty()) {
    return absl::InvalidArgumentError("pgn is required");
  }
  if (pgn.size() > kMaxPgnBytes) {
    return absl::InvalidArgumentError(
        absl::StrCat("pgn is too large: ", pgn.size(), " bytes (max ", kMaxPgnBytes, ")"));
  }

  absl::StatusOr<chess_cpp::ParsedGame> parsed = chess_cpp::ParseGame(pgn);
  if (!parsed.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("pgn did not parse: ", parsed.status().message()));
  }
  if (static_cast<int>(parsed->san_moves.size()) > kMaxPlies) {
    return absl::InvalidArgumentError(absl::StrCat("pgn has ", parsed->san_moves.size(),
                                                   " plies (max ", kMaxPlies,
                                                   "); no chess game is this long"));
  }

  absl::StatusOr<one_d4::GameFeatures> features = one_d4::Extract(*parsed);
  if (!features.ok()) {
    // The extractor only refuses input it cannot replay, which is the
    // caller's PGN and the caller's problem.
    return absl::InvalidArgumentError(features.status().message());
  }

  Analysis analysis;
  analysis.num_moves = features->num_moves;
  for (one_d4::MotifOccurrence& occurrence : features->occurrences) {
    if (occurrence.motif == one_d4::Motif::kAttack) continue;
    std::string name = absl::AsciiStrToLower(one_d4::ToString(occurrence.motif));
    analysis.occurrences[std::move(name)].push_back(std::move(occurrence));
  }
  return analysis;
}

}  // namespace one_d4_v2
