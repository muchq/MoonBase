#include "domains/games/apis/games_hub/move_coalescing.h"

#include "absl/strings/str_cat.h"

namespace games_hub {

opal::server::DeliveryClass MoveCoalescing::For(const std::string& recipient,
                                                const moonbase::games::LobbyUpdate& update) {
  if (const auto* moved = update.as_playerMoved_or_null()) {
    const auto generation = generation_.find(recipient);
    return opal::server::DeliveryClass::Coalesce(absl::StrCat(
        "moved:", generation == generation_.end() ? 0 : generation->second, ":", moved->playerId));
  }
  generation_[recipient] = ++next_;
  return opal::server::DeliveryClass::Reliable();
}

void MoveCoalescing::Forget(const std::string& recipient) { generation_.erase(recipient); }

}  // namespace games_hub
