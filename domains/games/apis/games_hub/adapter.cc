#include "domains/games/apis/games_hub/adapter.h"

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/wire_cards.h"

namespace games_hub {

StatsDeltas PlayedWon(const std::vector<std::string>& players, std::string_view winner) {
  StatsDeltas deltas;
  for (const std::string& id : players) {
    HubStore::StatsDelta delta;
    delta.player_id = id;
    delta.played = 1;
    delta.won = id == winner ? 1 : 0;
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

absl::Status HeldInHand(const std::vector<cards::Card>& hand,
                        const std::vector<cards::Card>& named) {
  for (const cards::Card& card : named) {
    if (std::find(hand.begin(), hand.end(), card) == hand.end()) {
      const moonbase::games::Card wire = WireCard(card);
      return absl::NotFoundError(absl::StrCat("not in your hand: ", wire.rank, wire.suit));
    }
  }
  return absl::OkStatus();
}

}  // namespace games_hub
