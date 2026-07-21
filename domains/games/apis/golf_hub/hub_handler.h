#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_HUB_HANDLER_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_HUB_HANDLER_H

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "moonbase/golf/server.h"
#include "smithy/server/session_registry.h"

namespace golf_hub {

/// The hub, phase 1: session admission and room lifecycle on the
/// smithy-cpp streaming stack. One SessionRegistry keyed by playerId
/// carries all fan-out (async delivery — no writer threads); rooms are a
/// mutex'd map, membership marked disconnected during ADR-0020 grace and
/// reaped by on_expired. Game rules arrive in phase 2.
///
/// Redaction discipline for phase 2 starts now: every broadcast goes
/// through the per-recipient Broadcast(ids, make) form, so per-viewer
/// state (hidden cards) has exactly one place to land and no
/// identical-bytes path can leak it.
class HubHandler final : public moonbase::golf::GolfHubAsyncHandler {
 public:
  using Registry = smithy::server::SessionRegistry<moonbase::golf::GolfEvents>;

  explicit HubHandler(std::shared_ptr<TicketVault> vault,
                      std::chrono::seconds grace_period = std::chrono::minutes(5));

  smithy::Outcome<moonbase::golf::GetSessionOutput> GetSession(
      const moonbase::golf::GetSessionInput& input,
      const smithy::server::RequestContext& context) override;

  smithy::eventstream::StreamTask Play(moonbase::golf::PlayInput input,
                                       moonbase::golf::PlayAsyncServerStream& stream) override;

  /// For main's SIGTERM path: Drain, then transport Stop.
  Registry& registry() { return registry_; }

 private:
  // All room state behind one mutex. connected=false marks a seat in
  // ADR-0020 grace; expiry removes it.
  struct RoomStateSnapshot {
    std::vector<std::string> member_ids;
    moonbase::golf::RoomState state;
  };

  void HandleCommand(const std::string& player_id, const moonbase::golf::GolfCommands& command);
  void SetConnected(const std::string& player_id, bool connected);
  std::optional<std::string> CurrentRoom(const std::string& player_id);
  // Removes the player from their room (if any); returns the room id when
  // the remaining members should be told.
  std::optional<std::string> LeaveCurrentRoom(const std::string& player_id);
  void BroadcastRoom(const std::string& room_id);
  void Reject(const std::string& player_id, std::string reason);
  void OnExpired(const std::string& player_id);
  std::optional<RoomStateSnapshot> SnapshotLocked(const std::string& room_id);

  const std::shared_ptr<TicketVault> vault_;
  std::mutex mu_;
  // roomId -> (playerId -> connected)
  std::unordered_map<std::string, std::unordered_map<std::string, bool>> rooms_;
  std::unordered_map<std::string, std::string> player_room_;
  // Declared last: destroyed first, joining registry threads before the
  // maps its on_expired callback touches go away.
  Registry registry_;
};

}  // namespace golf_hub

#endif
