#ifndef CPP_CARDS_RUMMY_GIN_SERDE_H
#define CPP_CARDS_RUMMY_GIN_SERDE_H

#include <string>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/rummy/gin.h"

namespace rummy {

/// A gin deal as a JSON object, the `deal` of a table row whose variant is
/// gin (table_serde). Versioned on its own; cards are Card::intValue().
[[nodiscard]] std::string serializeGinState(const GinState& state);

/// The deal back, refused (InvalidArgument) for any shape the engine could
/// not play: an unknown stage or phase, seats not two while playing, a
/// turn out of range, a card code out of range, a result that names seats
/// the deal does not have.
[[nodiscard]] absl::StatusOr<GinState> deserializeGinState(const std::string& serialized);

}  // namespace rummy

#endif
