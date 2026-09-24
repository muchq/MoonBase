#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_MOVE_COALESCING_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_MOVE_COALESCING_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "moonbase/games/types.h"
#include "opal/server/session_registry.h"

namespace games_hub {

/// The delivery class of a world update to one reader (opal #227). A
/// walker's moves coalesce, so a slow reader's queue holds at most one move
/// per walker and walking cannot fill it. Every other update is reliable
/// and starts the reader on a new key generation, so a move never replaces
/// one queued before that update. Generations are per reader.
class MoveCoalescing {
 public:
  opal::server::DeliveryClass For(const std::string& recipient,
                                  const moonbase::games::LobbyUpdate& update);

  /// Drops a reader that left the world; its next delivery is a snapshot,
  /// which starts a fresh generation.
  void Forget(const std::string& recipient);

  /// Readers tracked.
  std::size_t readers() const { return generation_.size(); }

 private:
  uint64_t next_ = 0;
  std::unordered_map<std::string, uint64_t> generation_;
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_MOVE_COALESCING_H
