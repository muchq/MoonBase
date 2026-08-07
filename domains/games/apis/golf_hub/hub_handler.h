#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_HUB_HANDLER_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_HUB_HANDLER_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "domains/games/apis/golf_hub/chat_store.h"
#include "domains/games/apis/golf_hub/hub_store.h"
#include "domains/games/apis/golf_hub/id_generator.h"
#include "domains/games/apis/golf_hub/rate_limiter.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/pg/listener.h"
#include "moonbase/golf/server.h"
#include "smithy/server/session_registry.h"

namespace golf_hub {

/// winners() restricted to the seats the roster still names — the
/// forfeit rule. A final state can carry a seat its roster has dropped:
/// an abandonment that ended the game keeps the departed hand so its
/// cards and score reach the scorecard (#1236), and such a seat cannot
/// win — however good its standing score, the game resolved against it.
/// For an ordinary finish the roster names every seat and this is
/// exactly the engine's rule, knocker-takes-ties included.
[[nodiscard]] std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                                   const std::vector<std::string>& roster);

/// The hub, phase 2 (#1187): session admission, rooms, chat, and the
/// game layer on the reshaped libs/cards/golf engine. One SessionRegistry
/// keyed by playerId carries all fan-out (async delivery — no writer
/// threads); rooms are a mutex'd map, membership marked disconnected
/// during ADR-0020 grace and reaped by on_expired — with one boot-time
/// grace for restored members no session ever reclaims (#1295), since
/// the registry that died with the old process took their timers.
///
/// Redaction discipline: every game broadcast goes through the
/// per-recipient Broadcast(ids, make) form with views built by ViewLocked
/// — per-viewer state (own peeks, the held draw) has exactly one place to
/// land and no identical-bytes path can leak it.
///
/// Chat observability (#1226): chat_appends{result}, chat_rows_delivered,
/// chat_catch_up_rows (a per-drain distribution — one observation's size
/// is how far behind its wake found this instance, the lag signal),
/// chat_history_replays, and chat_failures{stage}. Counts and stages
/// only: room ids, player ids, and message text never reach a metric
/// name or label, and the e2e suite sweeps for exactly that.
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
  /// `limits` are the per-session stream budgets (#1240); the defaults
  /// serve production and tests inject extremes to pin the behavior.
  explicit HubHandler(std::shared_ptr<TicketVault> vault,
                      std::shared_ptr<cards::Dealer> dealer = std::make_shared<cards::Dealer>(),
                      std::shared_ptr<IdGenerator> ids = std::make_shared<WhimsicalIdGenerator>(),
                      std::chrono::seconds grace_period = std::chrono::minutes(5),
                      std::shared_ptr<futility::otel::MetricsRecorder> metrics = nullptr,
                      std::shared_ptr<HubStore> store = nullptr,
                      std::shared_ptr<ChatStore> chat_store = nullptr, RateLimits limits = {});

  /// Joins the boot reaper before the members it walks are torn down.
  ~HubHandler() override;

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

  /// Boot-time restore of the store's snapshot: rooms, members (presence
  /// seeded from their rows), and games. Call before the
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
  /// (locals already heard the local fan-out). Chat wake-ups instead run
  /// the chat pump — including our own, which the cursor makes idempotent
  /// and which closes the ambiguous-commit path (an append whose
  /// connection died after COMMIT still reaches everyone).
  void OnNotify(const std::string& channel, const std::string& payload);

  /// The listener's channel-active target; runs on the listener's
  /// thread. Fired on first LISTEN and again after every reconnect's
  /// re-LISTEN — the "you may have missed notifications" signal.
  /// Chat channels pump from the cursor; room channels catch up with a
  /// re-read (#1276) but only re-project when rows actually moved, so a
  /// reconnect across many rooms does not flood session queues.
  void OnChannelActive(const std::string& channel);

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
  void Count(const char* name, const std::map<std::string, std::string>& attributes = {});

  /// Declares the unlabelled counters at construction so each exports a zero
  /// baseline; see the definition for why the two labelled ones are excluded.
  void DeclareMetrics();
  void TrackActive(int delta);
  void CountCommand(const moonbase::golf::GolfCommands& command);
  /// Every event leaves through here so stream_events sees each send.
  void Send(const std::string& player_id, moonbase::golf::GolfEvents event);

  /// Loads the room's retained history and sends one roomChatHistory to
  /// the just-admitted player — nobody else; the room already has it.
  /// Call outside mu_ (the load may reach a database) and after the
  /// stream's roomState, the order the model documents. A failed load is
  /// counted and skipped rather than failing the join: live delivery
  /// catches the client up from here, and overlap is legal anyway.
  void SendChatHistory(const std::string& room_id, const std::string& player_id);

  /// Births the room's chat cursor at the newest retained message id,
  /// inside the same mu_ hold that makes the room held — the reason no
  /// message can ever be skipped: an append cannot commit "behind" a
  /// cursor whose seed read shares the critical section that made its
  /// sender's membership visible, and everything at or below the seed
  /// predates every local member's history replay. A failed seed read
  /// leaves the cursor at 0, which fails toward re-delivering retained
  /// rows (clients dedupe by id) — never toward losing one. createRoom
  /// births its cursor directly at 0 instead: the room provably has no
  /// rows, and its creator's first append must not be read as the past.
  void SeedChatCursorLocked(const std::string& room_id);

  /// The one path every live chat row takes to local members: load pages
  /// above the room's cursor, stage rows to current members, advance,
  /// repeat while pages come back full. Local appends call this after
  /// their commit instead of staging directly, which is what makes a
  /// remote commit our append raced past (a lower id committed just
  /// before ours) reach locals in id order — a blind "deliver mine,
  /// advance cursor" would step over it. Remote wakes, our own wakes,
  /// duplicate wakes, and channel-active signals all funnel here too;
  /// the cursor makes every redundant call a cheap no-op, and a per-room
  /// in-flight flag collapses concurrent pumps into one. Cursors are
  /// only ever read here, never created: SeedChatCursorLocked births
  /// them with the room, so a missing cursor means a stale wake.
  ///
  /// Call outside mu_ (it takes mu_ itself, and reads may reach a
  /// database). A pump for a room this instance no longer holds delivers
  /// nothing and resurrects nothing.
  void PumpChat(const std::string& room_id);

  void SetConnected(const std::string& player_id, bool connected);
  std::optional<std::string> CurrentRoom(const std::string& player_id);
  Room* FindRoomLocked(const std::string& player_id);
  std::optional<GameRef> FindGameLocked(const std::string& player_id);
  /// Removes the player from their game and room (deliberate leave or
  /// grace expiry — never a mere close, which only parks the seat) and
  /// stages every notification that implies.
  void LeaveEverywhere(const std::string& player_id, Outbox& outbox, Writes& writes);
  void LeaveGameLocked(const std::string& player_id, Outbox& outbox, Writes& writes);
  void BroadcastRoom(const std::string& room_id);
  void Reject(const std::string& player_id, std::string reason);
  void OnExpired(const std::string& player_id);
  /// The reap decision both expiry paths share: one fresh read of the
  /// member's room, then LeaveEverywhere unless the row says connected —
  /// a row back at connected belongs to a live session somewhere (their
  /// new instance, or a sibling that held them all along) and must not
  /// be reaped from here. Returns whether the member was reaped.
  bool ReapUnlessResumedElsewhere(const std::string& player_id);
  /// Waits out one grace period from boot, then reaps every restored
  /// member no session reclaimed (#1295): the registry can only arm
  /// grace for seats it admitted, so without this a restart converts a
  /// parked member's five-minute grace into forever-membership — and a
  /// stale membership is what turns a share-link join into "room
  /// unavailable or already in a room" (muchq.github.io#260).
  void BootReaperMain();
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

  /// Shared chat/room dispatch for OnNotify and OnChannelActive.
  /// `from_active` skips the own-instance filter (active has no payload)
  /// and uses change-gated projection for rooms so reconnect catch-up
  /// does not re-fan identical state.
  void WakeChannel(const std::string& channel, const std::string& payload, bool from_active);

  /// Notify/active catch-up for a held room: Flush+LoadRoom off mu_
  /// (PumpChat's pattern), then reconcile under the lock. Keeps the
  /// listener poll thread from holding mu_ across DB round trips on a
  /// reconnect storm.
  void CatchUpRoom(const std::string& room_id, bool project_always);

  /// The wake handler's body: flush our own queue (so the read is never
  /// older than local truth), re-read the room's rows, reconcile, and
  /// re-project views to local members. Also the join path's fallback —
  /// it materializes a room another instance created. Callers hold mu_.
  void RefreshRoomLocked(const std::string& room_id, Outbox& outbox);
  /// Returns whether local membership/games changed. When
  /// `project_always` is false, skips re-project on a no-op catch-up.
  bool ReconcileRoomLocked(const std::string& room_id, const HubStore::RoomRows& rows,
                           Outbox& outbox, bool project_always = true);
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
  const RateLimits limits_;
  /// ADR-0020 reconnect grace, shared by the registry's per-seat timers
  /// and the boot reaper's one cohort deadline. Zero disables both.
  const std::chrono::seconds grace_period_;
  /// Rides every commit and rider as the notify payload, so an instance
  /// can tell its own wake-ups from the ones that carry news.
  const std::string instance_id_;
  std::mutex mu_;
  pg::Listener* listener_ = nullptr;  // owned by the caller; guarded by mu_
  std::unordered_map<std::string, Room> rooms_;

  /// Where live chat delivery stands for one held room. `delivered` is
  /// the highest message id every current local member has been staged.
  /// A cursor is born in the same mu_ critical section that makes its
  /// room held — SeedChatCursorLocked — and dies with the room, so every
  /// held room has one and PumpChat never creates them. `pumping`/`again`
  /// collapse concurrent PumpChat calls into one drain.
  struct ChatCursor {
    int64_t delivered = 0;
    bool pumping = false;
    bool again = false;
  };
  std::unordered_map<std::string, ChatCursor> chat_cursors_;

  std::unordered_map<std::string, std::string> player_room_;
  std::unordered_map<std::string, std::string> player_game_;

  /// The boot cohort (#1295): members RestoreFromStore rebuilt, minus
  /// everyone a session reclaimed since. Guarded by mu_; drained once by
  /// the boot reaper at boot + grace_period_. The stop flag has its own
  /// mutex so the destructor never contends with a reap in progress.
  std::unordered_set<std::string> restored_pending_;
  /// RestoreFromStore ran to completion — the real once-guard, since a
  /// restore that found nothing (or zero grace) arms no thread and a
  /// joinable() check would wave the second call through.
  bool restored_ = false;
  std::mutex reaper_mu_;
  std::condition_variable reaper_cv_;
  bool reaper_stop_ = false;
  std::thread boot_reaper_;

  // Declared last: destroyed first, joining registry threads before the
  // maps its on_expired callback touches go away. (The boot reaper is
  // joined earlier still, explicitly, in ~HubHandler.)
  Registry registry_;
};

}  // namespace golf_hub

#endif
