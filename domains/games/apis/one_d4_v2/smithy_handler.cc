#include "domains/games/apis/one_d4_v2/smithy_handler.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/one_d4_v2/analyze.h"
#include "domains/games/libs/chess_cpp/side.h"

namespace one_d4_v2 {
namespace {

namespace gen = moonbase::one_d4;

std::optional<std::string> OrAbsent(const std::optional<std::string>& value) {
  // "" and absent both serialize a field the caller must treat as missing;
  // absent is the one /v1/analyze sent.
  return value.has_value() && !value->empty() ? value : std::nullopt;
}

gen::AnalyzedOccurrence ToWire(const one_d4::MotifOccurrence& occurrence) {
  gen::AnalyzedOccurrence wire;
  wire.ply = occurrence.ply;
  wire.moveNumber = occurrence.move_number;
  wire.side = std::string(chess_cpp::ToString(occurrence.side));
  wire.description = occurrence.description;
  wire.movedPiece = OrAbsent(occurrence.moved_piece);
  wire.attacker = OrAbsent(occurrence.attacker);
  wire.target = OrAbsent(occurrence.target);
  wire.isDiscovered = occurrence.is_discovered;
  wire.isMate = occurrence.is_mate;
  if (occurrence.pin_type.has_value()) {
    wire.pinType = std::string(one_d4::ToString(*occurrence.pin_type));
  }
  return wire;
}

}  // namespace

smithy::Error ToSmithyError(const absl::Status& status) {
  const std::string message(status.message());
  if (status.code() == absl::StatusCode::kInvalidArgument) {
    smithy::Error error = smithy::Error::Modeled("InvalidPgnError", message);
    error.set_detail(gen::InvalidPgnError{.message = message});
    return error;
  }
  return smithy::Error::Unknown(message);
}

smithy::Outcome<gen::AnalyzeOutput> SmithyAnalyzeHandler::Analyze(
    const gen::AnalyzeInput& input,
    [[maybe_unused]] const smithy::server::RequestContext& context) {
  const absl::StatusOr<Analysis> analysis = one_d4_v2::Analyze(input.pgn);
  if (!analysis.ok()) {
    return ToSmithyError(analysis.status());
  }

  gen::AnalyzeOutput output;
  output.numMoves = analysis->num_moves;
  for (const auto& [motif, occurrences] : analysis->occurrences) {
    output.motifs.push_back(motif);
    std::vector<gen::AnalyzedOccurrence>& wire = output.occurrences[motif];
    wire.reserve(occurrences.size());
    for (const one_d4::MotifOccurrence& occurrence : occurrences) {
      wire.push_back(ToWire(occurrence));
    }
  }
  return output;
}

}  // namespace one_d4_v2
