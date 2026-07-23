#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_HUB_HANDLER_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_HUB_HANDLER_H

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "moonbase/golf/server.h"
#include "smithy/server/session_registry.h"

namespace golf_hub {

/// The hub, phase 2 (#1187): session admission, rooms, chat, and the
/// game layer on the reshaped libs/cards/golf engine. One SessionRegistry
/// keyed by playerId carries all fan-out (async delivery — no writer
/// threads); rooms are a mutex'd map, membership marked disconnected
/// during ADR-0020 grace and reaped by on_expired.
///
/// Redaction discipline: every game broadcast goes through the
/// per-recipient Broadcast(ids, make) form with views built by ViewLocked
/// — per-viewer state (own peeks, the held draw) has exactly one place to
/// land and no identical-bytes path can leak it.
class HubHandler final : public moonbase::golf::GolfHubAsyncHandler {
 public:
  using Registry = smithy::server::SessionRegistry<moonbase::golf::GolfEvents>;

  explicit HubHandler(std::shared_ptr<TicketVault> vault,
                      std::shared_ptr<cards::Dealer> dealer = std::make_shared<cards::Dealer>(),
                      std::chrono::seconds grace_period = std::chrono::minutes(5));

  // Note: operation IO generates as <Op>Input/<Op>Output regardless of
  // the named shapes bound in the model, and moonbase.games shapes land
  // in the moonbase::golf C++ namespace (codegen flattens the model into
  // the one namespace the BUILD rule names).
  smithy::Outcome<moonbase::golf::GetSessionOutput> GetSession(
      const moonbase::golf::GetSessionInput& input,
      const smithy::server::RequestContext& context) override;

  smithy::eventstream::StreamTask Play(moonbase::golf::PlayInput input,
                                       moonbase::golf::PlayAsyncServerStream& stream) override;

  /// For main's SIGTERM path: Drain, then transport Stop.
  Registry& registry() { return registry_; }

 private:
  struct Member {
    bool connected = true;
    int games_played = 0;
    int games_won = 0;
    int total_score = 0;
  };

  /// A game is a pre-start roster until startGame swaps in engine state.
  /// Once started, roster membership mirrors the engine's seats — every
  /// join/leave updates both, or a seat would stop receiving views.
  struct GameEntry {
    std::vector<std::string> roster;
    std::optional<golf::GameState> state;
    [[nodiscard]] bool started() const { return state.has_value(); }
  };

  struct Room {
    std::map<std::string, Member> members;
    std::map<std::string, GameEntry> games;
  };

  /// Events staged under the lock, delivered outside it. Delivery
  /// preserves staged order per recipient — callers stage in the order
  /// clients must observe (e.g. final views before gameEnded).
  struct Outbox {
    std::vector<std::pair<std::string, moonbase::golf::GolfEvents>> events;
    void To(const std::string& player_id, moonbase::golf::GolfEvents event) {
      events.emplace_back(player_id, std::move(event));
    }
  };

  using MoveFn = std::function<absl::StatusOr<golf::GameState>(const golf::GameState&, int seat)>;
  /// What a successful engine move announces beyond the state views.
  struct MoveEffects {
    bool announce_turn = false;   // turnChanged when the seat advances
    bool announce_knock = false;  // playerKnocked first
    bool peek_fanout = false;     // views to all only once the countdown starts
  };

  /// A player's room, game, and game entry resolved together; fields are
  /// non-null/engaged only as far as the player is actually placed.
  struct GameRef {
    std::string room_id;
    Room* room = nullptr;
    std::string game_id;
    GameEntry* entry = nullptr;
  };

  void HandleCommand(const std::string& player_id, const moonbase::golf::GolfCommands& command);
  void HandleMove(const std::string& player_id, const moonbase::golf::GolfMove& move);
  void CreateGameMove(const std::string& player_id);
  void JoinGameMove(const std::string& player_id, const std::string& game_id);
  void StartGameMove(const std::string& player_id);
  /// The shared shape of every in-game engine move: transition, then
  /// stage the fan-out (views, turn change, game end) the result implies.
  void EngineMove(const std::string& player_id, const MoveFn& move, MoveEffects effects);

  void SetConnected(const std::string& player_id, bool connected);
  std::optional<std::string> CurrentRoom(const std::string& player_id);
  Room* FindRoomLocked(const std::string& player_id);
  std::optional<GameRef> FindGameLocked(const std::string& player_id);
  /// Removes the player from their game and room (deliberate leave, clean
  /// close, or grace expiry) and stages every notification that implies.
  void LeaveEverywhere(const std::string& player_id, Outbox& outbox);
  void LeaveGameLocked(const std::string& player_id, Outbox& outbox);
  void BroadcastRoom(const std::string& room_id);
  void Reject(const std::string& player_id, std::string reason);
  void OnExpired(const std::string& player_id);
  void Deliver(Outbox& outbox);

  /// Builders; callers hold mu_.
  moonbase::golf::RoomState RoomStateLocked(const std::string& room_id, const Room& room) const;
  void StageRoomStateLocked(const std::string& room_id, Outbox& outbox) const;
  moonbase::golf::GameView ViewLocked(const std::string& game_id, const GameEntry& entry,
                                      const std::string& viewer_id) const;
  void StageGameViewsLocked(const std::string& game_id, const GameEntry& entry,
                            Outbox& outbox) const;
  void FinalizeGameLocked(const std::string& room_id, Room& room, const std::string& game_id,
                          Outbox& outbox);

  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<cards::Dealer> dealer_;
  std::mutex mu_;
  std::unordered_map<std::string, Room> rooms_;
  std::unordered_map<std::string, std::string> player_room_;
  std::unordered_map<std::string, std::string> player_game_;
  // Declared last: destroyed first, joining registry threads before the
  // maps its on_expired callback touches go away.
  Registry registry_;
};

}  // namespace golf_hub

#endif
