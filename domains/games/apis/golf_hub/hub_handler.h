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

#include "domains/games/apis/golf_hub/chat_store.h"
#include "domains/games/apis/golf_hub/hub_store.h"
#include "domains/games/apis/golf_hub/id_generator.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/pg/listener.h"
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
///
/// With a store attached, instances are interchangeable (#1194 step 3):
/// the database is every game's authority. A move loads nothing extra —
/// the local entry mirrors the stored row — but its result only counts
/// once the conditional commit lands; a miss rebases the entry from the
/// stored truth and retries the transition. Each commit's NOTIFY wakes
/// the other instances holding that room; a woken instance re-reads the
/// rows and re-projects views for its local players (redaction stays
/// local — only wake-ups cross the wire). Rooms and members keep the
/// step-2 async write-through (single writer per row, nothing to
/// conflict with), with notify riders so remote rosters converge. Chat
/// commits to its own ChatStore before it is echoed, so a message its
/// sender sees is a message that was stored; cross-instance chat
/// fan-out and join-time history replay are still to come (#1226).
class HubHandler final : public moonbase::golf::GolfHubAsyncHandler {
 public:
  using Registry = smithy::server::SessionRegistry<moonbase::golf::GolfEvents>;

  /// Every rooms/members/games mutation goes through HubStore. A null
  /// argument selects the production MemoryHubStore; PostgreSQL callers
  /// inject PgHubStore. Call RestoreFromStore before serving to rebuild
  /// rooms, members, and games.
  ///
  /// Chat has its own store because its write path shares nothing with
  /// the others. A null chat_store selects a MemoryChatStore authorized
  /// through WithMember below, which is what production runs today —
  /// chat is process-local until #1226 wires PgChatStore into main, so
  /// it neither survives a restart nor reaches another instance. A
  /// PgChatStore passed here authorizes against room_members in its own
  /// transaction and needs nothing from this handler.
  explicit HubHandler(std::shared_ptr<TicketVault> vault,
                      std::shared_ptr<cards::Dealer> dealer = std::make_shared<cards::Dealer>(),
                      std::shared_ptr<IdGenerator> ids = std::make_shared<WhimsicalIdGenerator>(),
                      std::chrono::seconds grace_period = std::chrono::minutes(5),
                      std::shared_ptr<futility::otel::MetricsRecorder> metrics = nullptr,
                      std::shared_ptr<HubStore> store = nullptr,
                      std::shared_ptr<ChatStore> chat_store = nullptr);

  /// Runs `action` while mu_ holds (room_id, player_id) to be a current
  /// member, or returns false without running it. Membership cannot
  /// change while the action runs, which is what lets a caller act on a
  /// seat it just checked instead of one that may already be gone.
  ///
  /// Nothing about this is chat-specific; chat is only its first caller.
  ///
  /// Callers must not already hold mu_ — it is not recursive, so calling
  /// this from under the lock deadlocks rather than blocking. That is
  /// why the chat path resolves the room, drops the lock, and lets the
  /// store re-take it through here.
  bool WithMember(const std::string& room_id, const std::string& player_id,
                  const MemberAction& action);

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

  /// Boot-time restore of the store's snapshot: rooms, members (everyone
  /// disconnected until they resume), and games. Call before the
  /// transport serves; no-op without a store. Undecodable rows were
  /// already dropped (loudly) by the store — an error here means the
  /// snapshot itself couldn't be read.
  absl::Status RestoreFromStore();

  /// Wires the fan-out's LISTEN side (#1194 step 3): subscribes every
  /// room the hub holds and follows rooms as they come and go. Call
  /// after RestoreFromStore, before serving, with a listener whose
  /// callback forwards to OnNotify. The caller owns the listener and
  /// must detach (attach nullptr) before destroying either object.
  void AttachListener(pg::Listener* listener);

  /// The listener callback target; runs on the listener's thread. A
  /// wake-up for a held room re-reads its rows and re-projects views to
  /// local players; payloads carrying our own instance id are skipped
  /// (locals already heard the local fan-out).
  void OnNotify(const std::string& channel, const std::string& payload);

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
  /// version is the entry's revision: every mutation bumps it through
  /// CommitEntryLocked (store or not), and with a store the commit only
  /// lands when the stored row holds the predecessor — that condition
  /// is what serializes instances.
  struct GameEntry {
    std::vector<std::string> roster;
    std::optional<golf::GameState> state;
    int64_t version = 0;
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

  /// Write-through ops staged under mu_ and — unlike the Outbox, whose
  /// delivery can block and so waits for unlock — handed to the store
  /// while still holding mu_ (Enqueue is a queue append, no I/O). That
  /// asymmetry is load-bearing: it is what makes queue order the truth's
  /// order when two mutations race.
  using Writes = std::vector<HubStore::Op>;

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

  /// Stream-side observability (#1187 phase 4): the aura chain instruments
  /// only unary requests, so admissions, live-session count, disconnects,
  /// and the command/event flow are counted here. All no-ops when no
  /// recorder is injected.
  /// Loads the room's retained history and sends one roomChatHistory to
  /// the just-admitted player — nobody else; the room already has it.
  /// Call outside mu_ (the load may reach a database) and after the
  /// stream's roomState, the order the model documents. A failed load is
  /// counted and skipped rather than failing the join: live delivery
  /// catches the client up from here, and overlap is legal anyway.
  void SendChatHistory(const std::string& room_id, const std::string& player_id);

  void Count(const char* name, const std::map<std::string, std::string>& attributes = {});
  void TrackActive(int delta);
  void CountCommand(const moonbase::golf::GolfCommands& command);
  /// Every event leaves through here so stream_events sees each send.
  void Send(const std::string& player_id, moonbase::golf::GolfEvents event);

  void SetConnected(const std::string& player_id, bool connected);
  std::optional<std::string> CurrentRoom(const std::string& player_id);
  Room* FindRoomLocked(const std::string& player_id);
  std::optional<GameRef> FindGameLocked(const std::string& player_id);
  /// Removes the player from their game and room (deliberate leave, clean
  /// close, or grace expiry) and stages every notification that implies.
  void LeaveEverywhere(const std::string& player_id, Outbox& outbox, Writes& writes);
  void LeaveGameLocked(const std::string& player_id, Outbox& outbox, Writes& writes);
  void BroadcastRoom(const std::string& room_id);
  void Reject(const std::string& player_id, std::string reason);
  void OnExpired(const std::string& player_id);
  void Deliver(Outbox& outbox);
  /// Hands staged ops to the store's writer queue. Callers hold mu_ (see
  /// Writes above). Asynchronous — clients may be told before the row
  /// lands. No-op without a store; always leaves writes empty.
  void EnqueueWritesLocked(Writes& writes);

  /// Write-through staging; callers hold mu_.
  void StageLocked(Writes& writes, HubStore::Op op) const;
  void StageMemberLocked(const std::string& room_id, const std::string& player_id,
                         const Member& member, Writes& writes) const;
  /// The async batch's wake-up rider: remote instances holding the room
  /// refresh once the writes staged before it have landed.
  void StageWakeLocked(const std::string& room_id, Writes& writes) const;

  /// One synchronous conditional commit of the entry's next revision
  /// (#1194 step 3). kCommitted adopts the candidate roster/state at
  /// version+1 (without a store this is the whole operation — memory
  /// stays the authority in single-instance mode). kRebased means
  /// another instance committed first: the entry now holds the stored
  /// truth — revalidate and retry. kGone: the game vanished remotely
  /// (the entry is untouched; the caller drops it). kUnavailable: the
  /// commit's fate is unknown; nothing was adopted.
  enum class Commit { kCommitted, kRebased, kGone, kUnavailable };
  Commit CommitEntryLocked(const std::string& room_id, const std::string& game_id, GameEntry& entry,
                           const std::vector<std::string>& roster,
                           const std::optional<golf::GameState>& state,
                           const std::vector<HubStore::StatsDelta>* finish);

  /// The wake handler's body: flush our own queue (so the read is never
  /// older than local truth), re-read the room's rows, reconcile, and
  /// re-project views to local members. Also the join path's fallback —
  /// it materializes a room another instance created. Callers hold mu_.
  void RefreshRoomLocked(const std::string& room_id, Outbox& outbox);
  void ReconcileRoomLocked(const std::string& room_id, const HubStore::RoomRows& rows,
                           Outbox& outbox);
  /// Erases the game and its player mappings without any events — for
  /// games the database says no longer exist.
  void DropGameLocked(const GameRef& ref);
  void ListenRoomLocked(const std::string& room_id);
  void UnlistenRoomLocked(const std::string& room_id);

  /// Builders; callers hold mu_.
  moonbase::golf::RoomState RoomStateLocked(const std::string& room_id, const Room& room) const;
  void StageRoomStateLocked(const std::string& room_id, Outbox& outbox) const;
  moonbase::golf::GameView ViewLocked(const std::string& game_id, const GameEntry& entry,
                                      const std::string& viewer_id) const;
  void StageGameViewsLocked(const std::string& game_id, const GameEntry& entry,
                            Outbox& outbox) const;
  /// The game-over ceremony: final face-up views, then gameEnded, then
  /// the game is erased locally. Shared by the local finisher and the
  /// refresh path (a game another instance finished).
  void StageGameOverLocked(const std::string& room_id, Room& room, const std::string& game_id,
                           Outbox& outbox);
  /// The local finisher: mirrors the stat deltas the finish commit
  /// already applied (or, without a store, applies them — same code)
  /// and runs the ceremony.
  void FinalizeGameLocked(const std::string& room_id, Room& room, const std::string& game_id,
                          Outbox& outbox);

  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<cards::Dealer> dealer_;
  const std::shared_ptr<IdGenerator> ids_;
  const std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const std::shared_ptr<HubStore> store_;
  const std::shared_ptr<ChatStore> chat_store_;
  /// Rides every commit and rider as the notify payload, so an instance
  /// can tell its own wake-ups from the ones that carry news.
  const std::string instance_id_;
  std::mutex mu_;
  pg::Listener* listener_ = nullptr;  // owned by the caller; guarded by mu_
  std::unordered_map<std::string, Room> rooms_;
  std::unordered_map<std::string, std::string> player_room_;
  std::unordered_map<std::string, std::string> player_game_;
  // Declared last: destroyed first, joining registry threads before the
  // maps its on_expired callback touches go away.
  Registry registry_;
};

}  // namespace golf_hub

#endif
