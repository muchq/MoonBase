#include "domains/games/apis/games_hub/golf_hub.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <set>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/apis/games_hub/protocol_input.h"
#include "domains/games/libs/cards/card_mapper.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/golf/player.h"

namespace games_hub {

using moonbase::games::CastleMove;
using moonbase::games::CastleUpdate;
using moonbase::games::GameCommands;
using moonbase::games::GameEvents;
using moonbase::games::GolfMove;
using moonbase::games::GolfUpdate;

std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                     const std::vector<std::string>& roster) {
  std::unordered_set<int> winning;
  int min_score = std::numeric_limits<int>::max();
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const std::string occupant = seat.getName().value_or("");
    if (std::find(roster.begin(), roster.end(), occupant) == roster.end()) continue;
    const int score = seat.score();
    if (score < min_score) {
      min_score = score;
      winning.clear();
    }
    if (score == min_score) winning.insert(static_cast<int>(i));
  }
  // The knocker takes ties alone — when still in contention. An
  // abandonment end carries no knocker (kAbandoned is never a seat).
  if (winning.contains(state.getWhoKnocked())) {
    winning.clear();
    winning.insert(state.getWhoKnocked());
  }
  return winning;
}

namespace {

constexpr std::size_t kMaxSeats = 4;

// The one place a stored row becomes a wire message. Live delivery and
// history replay share it, so the two can never describe the same
// message differently.
moonbase::games::ChatMessage ChatEvent(const ChatRow& row) {
  moonbase::games::ChatMessage message;
  message.messageId = row.message_id;
  message.playerId = row.player_id;
  message.text = row.text;
  message.sentAtUnixMillis = row.sent_at_unix_millis;
  return message;
}

// The v1 wire's card language, which the UI already renders. Ranks come
// from the canonical CardMapper table; suits are the wire's glyphs
// (CardMapper's letters are a different representation).
std::string SuitString(cards::Suit suit) {
  switch (suit) {
    case cards::Suit::Spades:
      return "♠";
    case cards::Suit::Hearts:
      return "♥";
    case cards::Suit::Diamonds:
      return "♦";
    case cards::Suit::Clubs:
      return "♣";
  }
  return "♠";
}

moonbase::games::Card WireCard(const cards::Card& card) {
  moonbase::games::Card wire;
  wire.rank = cards::CardMapper::rankToString(card.getRank());
  wire.suit = SuitString(card.getSuit());
  return wire;
}

std::string PhaseString(const golf::GameState& state) {
  if (state.isOver()) return "ended";
  if (state.revealCountdownActive()) return "peeking";
  if (state.getWhoKnocked() >= 0) return "knocked";
  return "playing";
}

// Seats are integer indexes; the occupant's identity is the player id.
std::string PlayerIdAt(const golf::GameState& state, int seat) {
  return state.getPlayer(seat).getName().value_or("");
}

GameEvents GolfUpdateEvent(GolfUpdate update) {
  moonbase::games::GolfEvent event;
  event.update = std::move(update);
  return GameEvents::FromGolf(std::move(event));
}

GameEvents CastleUpdateEvent(CastleUpdate update) {
  moonbase::games::CastleEvent event;
  event.update = std::move(update);
  return GameEvents::FromCastle(std::move(event));
}

// The shared lifecycle announcements, in the table's own envelope.
GameEvents CreatedEvent(GameKind kind, moonbase::games::GameCreated created) {
  return kind == GameKind::kCastle ? CastleUpdateEvent(CastleUpdate::FromGamecreated(created))
                                   : GolfUpdateEvent(GolfUpdate::FromGamecreated(created));
}
GameEvents StartedEvent(GameKind kind) {
  return kind == GameKind::kCastle
             ? CastleUpdateEvent(CastleUpdate::FromGamestarted(moonbase::games::GameStarted{}))
             : GolfUpdateEvent(GolfUpdate::FromGamestarted(moonbase::games::GameStarted{}));
}
GameEvents TurnEvent(GameKind kind, moonbase::games::TurnChanged turn) {
  return kind == GameKind::kCastle ? CastleUpdateEvent(CastleUpdate::FromTurnchanged(turn))
                                   : GolfUpdateEvent(GolfUpdate::FromTurnchanged(turn));
}
GameEvents LeftEvent(GameKind kind, moonbase::games::GameLeft left) {
  return kind == GameKind::kCastle ? CastleUpdateEvent(CastleUpdate::FromGameleft(left))
                                   : GolfUpdateEvent(GolfUpdate::FromGameleft(left));
}

// Bounded rebase-retry for the conditional commits: each miss adopts the
// stored truth, so giving up leaves a consistent hub and a client who
// can simply resend.
constexpr int kMaxCommitAttempts = 3;

std::string CastlePhaseString(const castle::GameState& state) {
  switch (state.getPhase()) {
    case castle::Phase::Setup:
      return "setup";
    case castle::Phase::Playing:
      return "playing";
    case castle::Phase::Over:
    case castle::Phase::Abandoned:
      return "ended";
  }
  return "ended";
}

std::string CastlePlayerIdAt(const castle::GameState& state, int seat) {
  if (seat < 0 || seat >= static_cast<int>(state.getPlayers().size())) return "";
  return state.getPlayer(seat).getId();
}

// The table's phase for the room's lobby summary, whichever game it plays.
std::string PhaseStringOf(const HostedState& state) {
  if (const auto* golf_state = std::get_if<golf::GameState>(&state)) {
    return PhaseString(*golf_state);
  }
  return CastlePhaseString(std::get<castle::GameState>(state));
}

// The occupant whose turn it is, or "" between turns and at the end.
std::string CurrentTurnOf(const HostedState& state) {
  if (const auto* golf_state = std::get_if<golf::GameState>(&state)) {
    return golf_state->isOver() ? "" : PlayerIdAt(*golf_state, golf_state->getWhoseTurn());
  }
  const auto& castle_state = std::get<castle::GameState>(state);
  return CastlePlayerIdAt(castle_state, castle_state.getWhoseTurn());
}

std::string InstanceId() {
  absl::BitGen gen;
  return absl::StrCat("hub-", absl::Hex(absl::Uniform<uint64_t>(gen), absl::kZeroPad16));
}

// The per-seat stat deltas a finished game applies — the payload of
// CommitGameFinish, and the same numbers FinalizeGameLocked mirrors into
// the local member rows. Roster seats only: an abandoned seat shows on
// the scorecard but tallies nothing.
std::vector<games_hub::HubStore::StatsDelta> StatsDeltas(const golf::GameState& state,
                                                         const std::vector<std::string>& roster) {
  const auto winner_indexes = WinnersAmong(state, roster);
  std::vector<games_hub::HubStore::StatsDelta> deltas;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const std::string occupant = seat.getName().value_or("");
    if (std::find(roster.begin(), roster.end(), occupant) == roster.end()) continue;
    games_hub::HubStore::StatsDelta delta;
    delta.player_id = occupant;
    delta.played = 1;
    delta.won = winner_indexes.contains(static_cast<int>(i)) ? 1 : 0;
    delta.score = seat.score();
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

// Castle's finish: the first out wins and everyone seated played. The
// engine compacts a leaver out of its seats, so every seat is on the
// roster; and a finish ends the game, so the winner never left. This
// leans on an over table never being left: StageGameOverLocked erases
// the entry before anyone could. No score: the game has none, so the
// room's running total stays put.
std::vector<games_hub::HubStore::StatsDelta> CastleStatsDeltas(const castle::GameState& state) {
  const std::string winner = state.getFinished().empty() ? "" : state.getFinished().front();
  std::vector<games_hub::HubStore::StatsDelta> deltas;
  for (const castle::Player& seat : state.getPlayers()) {
    games_hub::HubStore::StatsDelta delta;
    delta.player_id = seat.getId();
    delta.played = 1;
    delta.won = seat.getId() == winner ? 1 : 0;
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

std::vector<games_hub::HubStore::StatsDelta> StatsDeltasOf(const HostedState& state,
                                                           const std::vector<std::string>& roster) {
  if (const auto* golf_state = std::get_if<golf::GameState>(&state)) {
    return StatsDeltas(*golf_state, roster);
  }
  return CastleStatsDeltas(std::get<castle::GameState>(state));
}

// The seat a leaver vacates, in whichever engine: compacted while seats
// remain, or the game resolved (golf keeps every seat for the scorecard;
// castle abandons below two seats).
std::optional<HostedState> WithoutSeat(const HostedState& state, const std::string& player_id) {
  return std::visit(
      [&](const auto& engine) -> std::optional<HostedState> {
        const int seat = engine.playerIndex(player_id);
        if (seat < 0) return HostedState(engine);
        auto next = engine.removePlayer(seat);
        return HostedState(next.ok() ? *std::move(next) : engine);
      },
      state);
}

}  // namespace

GolfHub::GolfHub(std::shared_ptr<TicketVault> vault, std::shared_ptr<cards::Dealer> dealer,
                 std::shared_ptr<IdGenerator> ids, std::chrono::seconds grace_period,
                 std::shared_ptr<futility::otel::MetricsRecorder> metrics,
                 std::shared_ptr<HubStore> store, std::shared_ptr<ChatStore> chat_store,
                 RateLimits limits, GolfTestHooks hooks)
    : vault_(std::move(vault)),
      dealer_(std::move(dealer)),
      ids_(std::move(ids)),
      metrics_(std::move(metrics)),
      store_(store != nullptr ? std::move(store) : std::make_shared<MemoryHubStore>()),
      // Capturing this is safe: the guard is stored, not called, and
      // nothing appends chat before the handler is serving.
      chat_store_(chat_store != nullptr
                      ? std::move(chat_store)
                      : std::make_shared<MemoryChatStore>([this](const std::string& room_id,
                                                                 const std::string& player_id,
                                                                 const MemberAction& action) {
                          return WithMember(room_id, player_id, action);
                        })),
      limits_(limits),
      hooks_(std::move(hooks)),
      grace_period_(grace_period),
      instance_id_(InstanceId()),
      registry_([this, grace_period] {
        Registry::Options options;
        options.async_delivery = true;  // chains, not writer threads (ADR-0019)
        options.grace_period = grace_period;
        options.on_expired = [this](const std::string& id) { OnExpired(id); };
        return options;
      }()) {
  DeclareMetrics();
}

// Every counter series this handler can report, declared at construction so
// each exports a zero before it counts anything. Without a baseline the series
// is born carrying its first event's value, and increase() has nothing earlier
// to measure it against — so the first chat message, the first refused
// admission, the first reaped seat after a deploy are all invisible (#1323).
//
// Each entry is written out in full so it greps against its emit site as one
// string. That is the point of listing them at all: the declaration and the
// emit sites have to agree, and a reader checking that should not have to
// assemble a series out of two nested loops first.
//
// hub_e2e_test pins one of the two directions. ExpectOnlyDeclaredCounterSeries
// runs in every stream test's TearDown — and in every SecondInstance's
// destructor — so a series emitted but not declared fails, including from the
// tests that refuse admissions, trip the rate limiter or drop a stream, which
// is where most of these fire. The other direction, a value declared here that
// nothing ever emits, has no such guard: it costs a permanently flat line on a
// dashboard, and the only thing in its way is the literal copy of this list in
// BuildingAHandlerDeclaresEveryCounterSeriesAtZero, which has to be edited too
// — except for the golf_commands/golf_events blocks, whose values are the
// model's union cases and are pinned both ways against golf.smithy by
// StreamSeriesMatchTheModelUnions.
const std::vector<GolfHub::CounterSeries>& GolfHub::DeclaredCounterSeries() {
  static const auto* kSeries = new std::vector<CounterSeries>{
      {"castle_commands", {{"command", "createGame"}}},
      {"castle_commands", {{"command", "joinGame"}}},
      {"castle_commands", {{"command", "startGame"}}},
      {"castle_commands", {{"command", "leaveGame"}}},
      {"castle_commands", {{"command", "swapForSetup"}}},
      {"castle_commands", {{"command", "ready"}}},
      {"castle_commands", {{"command", "playFromHand"}}},
      {"castle_commands", {{"command", "playFaceUp"}}},
      {"castle_commands", {{"command", "playFaceDown"}}},
      {"castle_commands", {{"command", "pickUp"}}},
      {"castle_events", {{"event", "gameJoined"}}},
      {"castle_events", {{"event", "gameState"}}},
      {"castle_events", {{"event", "gameCreated"}}},
      {"castle_events", {{"event", "gameStarted"}}},
      {"castle_events", {{"event", "turnChanged"}}},
      {"castle_events", {{"event", "gameEnded"}}},
      {"castle_events", {{"event", "gameLeft"}}},
      {"chat_appends", {{"result", "stored"}}},
      {"chat_appends", {{"result", "rejected"}}},
      {"chat_appends", {{"result", "unavailable"}}},
      {"chat_catch_up_drains", {}},
      {"chat_failures", {{"stage", "cursor_seed"}}},
      {"chat_failures", {{"stage", "catch_up"}}},
      {"chat_failures", {{"stage", "history_load"}}},
      {"chat_history_replays", {}},
      {"chat_rows_delivered", {}},
      {"golf_admissions_refused", {{"reason", "bad_ticket"}}},
      {"golf_admissions_refused", {{"reason", "seat_conflict"}}},
      // The GameCommands union in model order, golf.<move> for the envelope —
      // CountCommand's naming, spelled out (see the model-pin note above).
      {"golf_commands", {{"command", "createRoom"}}},
      {"golf_commands", {{"command", "joinRoom"}}},
      {"golf_commands", {{"command", "leaveRoom"}}},
      {"golf_commands", {{"command", "getRoomState"}}},
      {"golf_commands", {{"command", "chat"}}},
      {"golf_commands", {{"command", "golf.createGame"}}},
      {"golf_commands", {{"command", "golf.joinGame"}}},
      {"golf_commands", {{"command", "golf.startGame"}}},
      {"golf_commands", {{"command", "golf.leaveGame"}}},
      {"golf_commands", {{"command", "golf.peekCard"}}},
      {"golf_commands", {{"command", "golf.drawCard"}}},
      {"golf_commands", {{"command", "golf.takeFromDiscard"}}},
      {"golf_commands", {{"command", "golf.swapCard"}}},
      {"golf_commands", {{"command", "golf.discardDrawn"}}},
      {"golf_commands", {{"command", "golf.knock"}}},
      {"golf_commands", {{"command", "golf.hideCards"}}},
      {"golf_disconnects", {{"kind", "clean"}}},
      {"golf_disconnects", {{"kind", "abrupt"}}},
      // The GameEvents union in model order, golf.<update> for the envelope —
      // Send's naming.
      {"golf_events", {{"event", "sessionReady"}}},
      {"golf_events", {{"event", "roomState"}}},
      {"golf_events", {{"event", "roomLeft"}}},
      {"golf_events", {{"event", "roomChat"}}},
      {"golf_events", {{"event", "roomChatHistory"}}},
      {"golf_events", {{"event", "commandRejected"}}},
      {"golf_events", {{"event", "golf.gameJoined"}}},
      {"golf_events", {{"event", "golf.gameState"}}},
      {"golf_events", {{"event", "golf.gameCreated"}}},
      {"golf_events", {{"event", "golf.gameStarted"}}},
      {"golf_events", {{"event", "golf.turnChanged"}}},
      {"golf_events", {{"event", "golf.playerKnocked"}}},
      {"golf_events", {{"event", "golf.gameEnded"}}},
      {"golf_events", {{"event", "golf.gameLeft"}}},
      // The lobby envelope on its own series, castle's precedent: the
      // LobbyAction and LobbyUpdate cases, pinned by
      // LobbySeriesMatchTheModelUnions.
      {"lobby_commands", {{"command", "join"}}},
      {"lobby_commands", {{"command", "move"}}},
      {"lobby_commands", {{"command", "shape"}}},
      {"lobby_commands", {{"command", "leave"}}},
      {"lobby_events", {{"event", "worldState"}}},
      {"lobby_events", {{"event", "playerJoined"}}},
      {"lobby_events", {{"event", "playerMoved"}}},
      {"lobby_events", {{"event", "shapeChanged"}}},
      {"lobby_events", {{"event", "playerLeft"}}},
      {"golf_rate_limited", {{"kind", "chat"}}},
      {"golf_rate_limited", {{"kind", "command"}}},
      // One entry per RejectKind, in enum order.
      {"golf_rejections", {{"kind", "rate_limited"}}},
      {"golf_rejections", {{"kind", "invalid"}}},
      {"golf_rejections", {{"kind", "state"}}},
      {"golf_rejections", {{"kind", "rules"}}},
      {"golf_rejections", {{"kind", "unavailable"}}},
      {"golf_rejections", {{"kind", "unknown"}}},
      {"golf_restored_seats_reaped", {}},
      {"golf_seats_expired", {}},
      {"golf_sessions", {{"resumed", "true"}}},
      {"golf_sessions", {{"resumed", "false"}}},
  };
  return *kSeries;
}

void GolfHub::DeclareMetrics() {
  if (!metrics_) return;
  for (const CounterSeries& series : DeclaredCounterSeries()) {
    metrics_->DeclareCounter(series.name, series.attributes);
  }
}

GolfHub::~GolfHub() {
  {
    const std::lock_guard<std::mutex> lock(reaper_mu_);
    reaper_stop_ = true;
  }
  reaper_cv_.notify_all();
  if (boot_reaper_.joinable()) boot_reaper_.join();
}

absl::Status GolfHub::RestoreFromStore() {
  // Once, before serving. A second restore would stack a new cohort
  // behind a reaper that never re-arms — forever-membership again, the
  // exact silent shape #1295 closed — so refuse it loudly instead.
  if (restored_) {
    return absl::FailedPreconditionError("RestoreFromStore already ran");
  }
  auto snapshot = store_->LoadSnapshot();
  if (!snapshot.ok()) return snapshot.status();
  const std::lock_guard<std::mutex> lock(mu_);
  for (const std::string& room_id : snapshot->rooms) rooms_[room_id];
  for (const HubStore::MemberRow& row : snapshot->members) {
    // Presence seeds from the row, the fleet truth ReconcileRoomLocked
    // already adopts on every wake. Seeding false here instead made the
    // first channel-active after a boot see phantom movement and inject
    // a roomState into a resuming seat's hydration (#1276 sighting #4).
    // A member whose socket died with the old process reads connected
    // until they resume or leave; no one owns flipping a crashed
    // instance's rows, and inventing a local answer just diverges from
    // what every other instance projects.
    Member member;
    member.connected = row.connected;
    member.games_played = row.games_played;
    member.games_won = row.games_won;
    member.total_score = row.total_score;
    rooms_[row.room_id].members.emplace(row.player_id, member);
    player_room_[row.player_id] = row.room_id;
    // Only parked rows enter the boot cohort. A row restored connected
    // is either a sibling's live seat — whose registry owns its grace,
    // and whose later park must run that full grace, not the remainder
    // of ours — or a connected-at-crash ghost the drain's row check
    // would spare anyway (#1295 records that residue).
    if (!row.connected) restored_pending_.insert(row.player_id);
  }
  for (HubStore::GameRow& row : snapshot->games) {
    if (row.state.has_value() && IsOver(*row.state)) {
      // Terminal rows are durable handoffs for live instances that may
      // not have processed the finish wake yet. A restart ignores them
      // locally so ceremonies do not replay, but room deletion owns
      // their eventual cleanup through the foreign-key cascade.
      continue;
    }
    GameEntry entry;
    entry.kind = row.kind;
    entry.roster = row.roster;
    entry.version = row.version;
    if (row.state.has_value()) entry.state.emplace(*std::move(row.state));
    for (const std::string& member_id : entry.roster) player_game_[member_id] = row.game_id;
    rooms_[row.room_id].games.emplace(row.game_id, std::move(entry));
  }
  // Every restored room gets its cursor before anything can pump it —
  // still inside the boot lock, before the listener or any stream exists.
  for (const auto& [room_id, room] : rooms_) SeedChatCursorLocked(room_id);
  LOG(INFO) << "hub restored " << snapshot->rooms.size() << " rooms, " << snapshot->members.size()
            << " members, " << snapshot->games.size() << " games";
  // The old process's registry took every parked member's grace timer
  // with it; this cohort deadline is their replacement (#1295). Zero
  // grace keeps it disabled, matching the registry's own contract.
  if (!restored_pending_.empty() && grace_period_ > std::chrono::seconds(0)) {
    boot_reaper_ = std::thread([this] { BootReaperMain(); });
  }
  restored_ = true;
  return absl::OkStatus();
}

void GolfHub::BootReaperMain() {
  {
    std::unique_lock<std::mutex> lock(reaper_mu_);
    reaper_cv_.wait_for(lock, grace_period_, [this] { return reaper_stop_; });
    if (reaper_stop_) return;
  }
  std::vector<std::string> pending;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    pending.assign(restored_pending_.begin(), restored_pending_.end());
    restored_pending_.clear();
  }
  for (const std::string& player_id : pending) {
    // Re-check stop between reaps: each one is a store round trip, and
    // a destructor landing mid-drain should wait out one, not all.
    {
      const std::lock_guard<std::mutex> lock(reaper_mu_);
      if (reaper_stop_) return;
    }
    // A resume that races this drain lands coherently in either order:
    // if its SetConnected write is in before the reap's flushed read,
    // the row spares the seat; if the reap wins mu_ first, the seat is
    // reaped and the resume admits room-less — the same outcome as
    // resuming a moment after the deadline.
    if (ReapUnlessResumedElsewhere(player_id)) Count("golf_restored_seats_reaped");
  }
}

void GolfHub::SeedChatCursorLocked(const std::string& room_id) {
  auto rows = chat_store_->LoadRecent(room_id, 1);
  ChatCursor cursor;
  if (rows.ok()) {
    cursor.delivered = rows->empty() ? 0 : rows->back().message_id;
  } else {
    // Fail toward duplication, never loss: a cursor left at 0 re-delivers
    // retained rows, which clients dedupe by id; guessing high would
    // swallow messages.
    Count("chat_failures", {{"stage", "cursor_seed"}});
    LOG(WARNING) << "chat cursor seed failed: " << rows.status();
  }
  chat_cursors_.try_emplace(room_id, cursor);
}

void GolfHub::EnqueueWritesLocked(Writes& writes) {
  if (!writes.empty()) store_->Enqueue(std::move(writes));
  writes.clear();
}

void GolfHub::StageLocked(Writes& writes, HubStore::Op op) const {
  writes.push_back(std::move(op));
}

void GolfHub::StageMemberLocked(const std::string& room_id, const std::string& player_id,
                                const Member& member, Writes& writes) const {
  HubStore::MemberRow row;
  row.room_id = room_id;
  row.player_id = player_id;
  row.connected = member.connected;
  row.games_played = member.games_played;
  row.games_won = member.games_won;
  row.total_score = member.total_score;
  writes.push_back(HubStore::UpsertMember{std::move(row)});
}

void GolfHub::StageWakeLocked(const std::string& room_id, Writes& writes) const {
  StageLocked(writes, HubStore::Notify{RoomChannel(room_id), instance_id_});
}

GolfHub::Commit GolfHub::CommitEntryLocked(const std::string& room_id, const std::string& game_id,
                                           GameEntry& entry, const std::vector<std::string>& roster,
                                           const std::optional<HostedState>& state,
                                           const std::vector<HubStore::StatsDelta>* finish) {
  const int64_t version = entry.version + 1;
  // The async outbox may still hold rows this commit depends on — the
  // room behind the insert's FK, the membership behind the finish's
  // stat deltas. Drain it so the synchronous write never outruns the
  // queued ones. (The writer needs no lock we hold.)
  store_->Flush();
  HubStore::GameRow row;
  row.room_id = room_id;
  row.game_id = game_id;
  row.kind = entry.kind;
  row.roster = roster;
  if (state.has_value()) row.state.emplace(*state);
  row.version = version;
  const auto landed = finish != nullptr ? store_->CommitGameFinish(row, *finish, instance_id_)
                                        : store_->CommitGameSave(row, instance_id_);
  if (!landed.ok()) {
    LOG(WARNING) << "game " << room_id << "/" << game_id
                 << " commit unavailable: " << landed.status();
    return Commit::kUnavailable;
  }
  if (!*landed) {
    auto stored = store_->LoadGame(room_id, game_id);
    if (!stored.ok()) {
      LOG(WARNING) << "game " << room_id << "/" << game_id
                   << " rebase read failed: " << stored.status();
      return Commit::kUnavailable;
    }
    if (!stored->has_value()) return Commit::kGone;
    entry.kind = (*stored)->kind;
    entry.roster = (*stored)->roster;
    entry.version = (*stored)->version;
    if ((*stored)->state.has_value()) entry.state.emplace(*std::move((*stored)->state));
    return Commit::kRebased;
  }
  entry.roster = roster;
  if (state.has_value()) entry.state.emplace(*state);
  entry.version = version;
  return Commit::kCommitted;
}

void GolfHub::AttachListener(pg::Listener* listener) {
  const std::lock_guard<std::mutex> lock(mu_);
  listener_ = listener;
  if (listener_ == nullptr) return;
  for (const auto& [room_id, room] : rooms_) {
    listener_->Listen(RoomChannel(room_id));
    listener_->Listen(ChatChannel(room_id));
  }
}

void GolfHub::OnNotify(const std::string& channel, const std::string& payload) {
  WakeChannel(channel, payload, /*from_active=*/false);
}

void GolfHub::OnChannelActive(const std::string& channel) {
  WakeChannel(channel, /*payload=*/"", /*from_active=*/true);
}

void GolfHub::WakeChannel(const std::string& channel, const std::string& payload,
                          bool from_active) {
  constexpr std::string_view kChatPrefix = "chat_";
  if (absl::StartsWith(channel, kChatPrefix)) {
    // Own wakes pump too, deliberately: the cursor makes it a no-op in
    // the common case, and it is what recovers an append whose
    // connection died between COMMIT and the reply. Active has no
    // payload, so the own-instance filter never applies here.
    PumpChat(std::string(channel.substr(kChatPrefix.size())));
    return;
  }
  // Notify echoes of our own commit are skipped; locals already heard
  // them. Active signals carry no instance id — always a catch-up.
  if (!from_active && payload == instance_id_) return;
  constexpr std::string_view kPrefix = "room_";
  if (!absl::StartsWith(channel, kPrefix)) return;
  // Notify always re-projects (wake contract). Active only projects when
  // rows moved — reconnect re-LISTENs every room and must not flood.
  CatchUpRoom(std::string(channel.substr(kPrefix.size())), /*project_always=*/!from_active);
}

void GolfHub::CatchUpRoom(const std::string& room_id, bool project_always) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // Only rooms we hold: a stale wake for a dropped room must not
    // resurrect it (join is the one path that materializes rooms).
    if (!rooms_.contains(room_id)) return;
  }
  // Off mu_: reconnect fires this once per room on the poll thread; holding
  // the lock across Flush+LoadRoom would stall every move/chat/join for
  // the whole burst. Same shape as PumpChat.
  store_->Flush();
  auto rows = store_->LoadRoom(room_id);
  if (!rows.ok()) {
    LOG(WARNING) << "room " << room_id << " catch-up failed: " << rows.status();
    return;
  }
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    if (!rooms_.contains(room_id)) return;
    ReconcileRoomLocked(room_id, *rows, outbox, project_always);
  }
  Deliver(outbox);
}

bool GolfHub::RefreshRoomLocked(const std::string& room_id, Outbox& outbox) {
  // The flush means this read can never be older than our own truth;
  // holding mu_ across it means nothing local moves in between. The
  // writer thread needs no lock we hold, so it drains freely. Join uses
  // this path to materialize; notify/active use CatchUpRoom instead.
  store_->Flush();
  auto rows = store_->LoadRoom(room_id);
  if (!rows.ok()) {
    // A missed wake is fine by protocol: the next read heals.
    LOG(WARNING) << "room " << room_id << " refresh failed: " << rows.status();
    return false;
  }
  ReconcileRoomLocked(room_id, *rows, outbox, /*project_always=*/true);
  return true;
}

bool GolfHub::ReconcileRoomLocked(const std::string& room_id, const HubStore::RoomRows& rows,
                                  Outbox& outbox, bool project_always) {
  if (!rows.exists) {
    // Deleted by another instance; nothing local can outrank that.
    const auto room = rooms_.find(room_id);
    if (room == rooms_.end()) return false;
    for (const auto& [member_id, member] : room->second.members) {
      if (auto it = player_room_.find(member_id);
          it != player_room_.end() && it->second == room_id) {
        player_room_.erase(it);
        player_game_.erase(member_id);
        LeaveWorldLocked(member_id, outbox);  // its world goes with it
      }
    }
    rooms_.erase(room);
    // Under mu_ on purpose. The membership guard holds this lock for the
    // whole of an append, so no append can be mid-flight here and land a
    // message after the drop.
    chat_store_->DropRoom(room_id);
    chat_cursors_.erase(room_id);
    UnlistenRoomLocked(room_id);
    return true;
  }

  const bool materialized = !rooms_.contains(room_id);
  Room& room = rooms_[room_id];
  ListenRoomLocked(room_id);  // idempotent; covers a room just materialized
  // A cursor born in the same critical section that makes the room held:
  // no member exists locally yet, so no append can commit behind it and
  // then be stepped over — the adoption race a wake-time seed would have.
  if (materialized) SeedChatCursorLocked(room_id);

  bool changed = materialized;

  // Members mirror the rows: every instance writes its own players'
  // rows, and the refresh flush made ours current.
  std::map<std::string, Member> members;
  for (const HubStore::MemberRow& row : rows.members) {
    Member member;
    member.connected = row.connected;
    member.games_played = row.games_played;
    member.games_won = row.games_won;
    member.total_score = row.total_score;
    members.emplace(row.player_id, member);
    player_room_[row.player_id] = room_id;
    // A row observed connected is a seat some session owns, and that
    // session's registry owns its grace from here on. Ceding the boot
    // cohort's claim is the remote mirror of Play's local erase: a
    // member who resumed on a sibling during our boot window — and may
    // park again before our deadline — gets their full grace from the
    // sibling's registry, not the remainder of ours (#1295). This is a
    // sample, not a subscription: a resume-and-repark whose connected
    // pulse no wake ever showed us stays in the cohort and reaps at our
    // deadline — closing that for real needs row-level presence
    // ownership, the same schema gap #1295's residue note records.
    if (row.connected) restored_pending_.erase(row.player_id);
  }
  if (members.size() != room.members.size()) {
    changed = true;
  } else {
    for (const auto& [id, member] : members) {
      const auto prior = room.members.find(id);
      if (prior == room.members.end() || prior->second.connected != member.connected ||
          prior->second.games_played != member.games_played ||
          prior->second.games_won != member.games_won ||
          prior->second.total_score != member.total_score) {
        changed = true;
        break;
      }
    }
  }
  for (const auto& [member_id, member] : room.members) {
    if (members.contains(member_id)) continue;
    changed = true;
    if (auto it = player_room_.find(member_id); it != player_room_.end() && it->second == room_id) {
      player_room_.erase(it);
      player_game_.erase(member_id);
      LeaveWorldLocked(member_id, outbox);  // a sibling's drop takes the world too
    }
  }
  room.members = std::move(members);

  // Games: adopt rows that moved past us. A row that ended while we
  // hold the game is a remote finish — ceremony here, then it is gone
  // locally (the finisher owns the deferred row delete); an ended row
  // for a game we no longer hold is that delete still pending.
  std::set<std::string> stored_ids;
  for (const HubStore::GameRow& row : rows.games) {
    stored_ids.insert(row.game_id);
    const bool over = row.state.has_value() && IsOver(*row.state);
    const auto game = room.games.find(row.game_id);
    if (game == room.games.end()) {
      if (over) continue;
      GameEntry entry;
      entry.kind = row.kind;
      entry.roster = row.roster;
      entry.version = row.version;
      if (row.state.has_value()) entry.state.emplace(*row.state);
      for (const std::string& member_id : entry.roster) player_game_[member_id] = row.game_id;
      room.games.emplace(row.game_id, std::move(entry));
      changed = true;
      continue;
    }
    GameEntry& entry = game->second;
    // A commit loop that rebased onto a remote finish holds the ended
    // state at the finisher's version, and its caller only refused or
    // left: the ceremony is still owed. A finished entry still held is
    // that debt, whatever the versions say, and the finished row pays it.
    const bool owed = entry.started() && IsOver(*entry.state);
    if (row.version <= entry.version && !owed) continue;  // ours is current
    changed = true;
    // A code re-minted for a table of the other game: the kind travels
    // with the state, or the next view would read the wrong engine.
    entry.kind = row.kind;
    for (const std::string& member_id : entry.roster) {
      // A member the new roster dropped left the game remotely.
      if (std::find(row.roster.begin(), row.roster.end(), member_id) == row.roster.end()) {
        if (auto it = player_game_.find(member_id);
            it != player_game_.end() && it->second == row.game_id) {
          player_game_.erase(it);
        }
      }
    }
    entry.roster = row.roster;
    entry.version = row.version;
    if (row.state.has_value()) entry.state.emplace(*row.state);
    for (const std::string& member_id : entry.roster) player_game_[member_id] = row.game_id;
    if (over) StageGameOverLocked(room, row.game_id, outbox);
  }
  for (auto game = room.games.begin(); game != room.games.end();) {
    if (stored_ids.contains(game->first)) {
      ++game;
      continue;
    }
    changed = true;
    // Deleted remotely: a disbanded lobby or a finished game's cleanup.
    for (const std::string& member_id : game->second.roster) {
      if (auto it = player_game_.find(member_id);
          it != player_game_.end() && it->second == game->first) {
        player_game_.erase(it);
      }
    }
    game = room.games.erase(game);
  }

  // Notify wake contract: always re-project. Active catch-up: only when
  // something moved, so a no-op reconnect does not fill session queues.
  if (changed || project_always) {
    for (const auto& [game_id, entry] : room.games) StageGameViewsLocked(game_id, entry, outbox);
    StageRoomStateLocked(room_id, outbox);
  }
  return changed;
}

void GolfHub::DropGameLocked(const GameRef& ref) {
  for (const std::string& member_id : ref.entry->roster) {
    if (auto it = player_game_.find(member_id);
        it != player_game_.end() && it->second == ref.game_id) {
      player_game_.erase(it);
    }
  }
  ref.room->games.erase(ref.game_id);
}

void GolfHub::ListenRoomLocked(const std::string& room_id) {
  if (listener_ == nullptr) return;
  listener_->Listen(RoomChannel(room_id));
  listener_->Listen(ChatChannel(room_id));
}

void GolfHub::UnlistenRoomLocked(const std::string& room_id) {
  if (listener_ == nullptr) return;
  listener_->Unlisten(RoomChannel(room_id));
  listener_->Unlisten(ChatChannel(room_id));
}

smithy::eventstream::StreamTask GolfHub::Play(moonbase::games::PlayInput input,
                                              moonbase::games::PlayAsyncServerStream& stream) {
  if (HasEmbeddedNul(input.ticket)) {
    Count("golf_admissions_refused", {{"reason", "bad_ticket"}});
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  auto player = vault_->SpendTicket(input.ticket);
  if (!player.has_value()) {
    Count("golf_admissions_refused", {{"reason", "bad_ticket"}});
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  const std::string player_id = *player;

  // The blessed admission call (ADR-0022): pre-first-suspend, on the
  // launching handler thread, where its brief blocking is legal.
  const auto admission = registry_.ResumeOrAdd(
      player_id, [&stream] { return stream.Share(); }, std::chrono::seconds(1));
  if (admission == Registry::Admission::kRefused) {
    Count("golf_admissions_refused", {{"reason", "seat_conflict"}});
    co_return smithy::Error::Modeled("SeatConflict", "player already has a live connection");
  }
  Count("golf_sessions",
        {{"resumed", admission == Registry::Admission::kResumed ? "true" : "false"}});
  TrackActive(+1);

  // Membership decides the resync, not the registry: a player restored
  // from the store (#1194 step 2) admits as new — their old process's
  // registry died — but their room and game are right here.
  std::optional<std::string> room;
  Outbox resync;
  SetConnected(player_id, true);
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // A reclaimed seat leaves the boot cohort: its lifecycle belongs to
    // the registry's own grace from here on (#1295).
    restored_pending_.erase(player_id);
    const auto room_it = player_room_.find(player_id);
    if (room_it != player_room_.end()) room = room_it->second;
    // A resumed seat mid-game gets its current view back immediately.
    if (auto ref = FindGameLocked(player_id)) {
      resync.To(player_id, JoinedEventLocked(ref->game_id, *ref->entry, player_id));
    }
  }
  const bool resumed = admission == Registry::Admission::kResumed || room.has_value();
  moonbase::games::SessionReady ready;
  ready.playerId = player_id;
  ready.resumed = resumed;
  if (room.has_value()) ready.roomId = *room;
  Send(player_id, GameEvents::FromSessionready(std::move(ready)));
  // A resumed seat's room sees the connected flip (and the resumer gets
  // the current snapshot it missed) — then the chat it missed, after its
  // roomState per the documented order, and only to this stream. The
  // resync game view goes last, deliberately: it is delivered after the
  // history load, whether or not that load succeeds.
  if (room.has_value()) {
    BroadcastRoom(*room);
    SendChatHistory(*room, player_id);
  }
  Deliver(resync);

  // Per-session budgets (#1240), owned by this coroutine frame: frames
  // are handled sequentially per session, so no locking, and the state
  // dies with the connection. Every frame draws from the command
  // bucket; chat draws from its own tighter bucket too, because each
  // message is a durable database transaction plus fleet-wide fan-out.
  TokenBucket command_budget(limits_.command_burst, limits_.command_refill_per_sec);
  TokenBucket chat_budget(limits_.chat_burst, limits_.chat_refill_per_sec);

  while (true) {
    auto received = co_await stream.Receive();
    if (!received.ok() || !received->has_value()) {
      // Any close parks the seat for the grace window (ADR-0020) —
      // expiry reaps it, a resume reclaims it. A clean close carries no
      // leave intent: a closed tab sends the same close frame (#1236),
      // and the deliberate exit is the explicit leaveRoom command.
      // Detach fails only when the entry is already gone — nothing left
      // to do then.
      // The world is presence, not membership: the player leaves it now
      // and rejoins on resume, while the seat parks. Before Detach, so a
      // resume admitted the instant the seat is released finds no stale
      // entry to refuse its join (the Think stream's close path keeps the
      // same order for the same reason).
      Outbox left;
      {
        const std::lock_guard<std::mutex> lock(mu_);
        LeaveWorldLocked(player_id, left);
      }
      Deliver(left);
      if (hooks_.before_seat_release) hooks_.before_seat_release(player_id);
      if (registry_.Detach(player_id)) {
        TrackActive(-1);
        Count("golf_disconnects", {{"kind", received.ok() ? "clean" : "abrupt"}});
        SetConnected(player_id, false);
        if (auto current = CurrentRoom(player_id)) BroadcastRoom(*current);
      }
      co_return smithy::Unit{};
    }
    const auto now = std::chrono::steady_clock::now();
    if (!command_budget.Admit(now)) {
      // Refused before any locked work: the whole point is that a flood
      // costs the hub almost nothing. The session stays open — the
      // buckets already bound the damage, and closing floods just
      // converts them into reconnect load.
      Count("golf_rate_limited", {{"kind", "command"}});
      Reject(player_id, RejectKind::kRateLimited, "slow down");
      continue;
    }
    if ((*received)->as_chat_or_null() != nullptr && !chat_budget.Admit(now)) {
      Count("golf_rate_limited", {{"kind", "chat"}});
      Reject(player_id, RejectKind::kRateLimited, "slow down");
      continue;
    }
    HandleCommand(player_id, **received);
  }
}

void GolfHub::HandleCommand(const std::string& player_id, const GameCommands& command) {
  CountCommand(command);
  if (command.as_createRoom_or_null() != nullptr) {
    std::string room_id;
    Outbox outbox;
    Writes writes;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!player_room_.contains(player_id)) {
        room_id = ids_->RoomId();
        while (rooms_.contains(room_id)) room_id = ids_->RoomId();
        const auto [member, inserted] = rooms_[room_id].members.emplace(player_id, Member{});
        // Born at zero with its room: it provably has no rows, and the
        // creator's first message must pump from the very beginning.
        chat_cursors_.emplace(room_id, ChatCursor{});
        LeaveWorldLocked(player_id, outbox);  // out of the plaza's world
        player_room_[player_id] = room_id;
        ListenRoomLocked(room_id);
        StageLocked(writes, HubStore::UpsertRoom{room_id});
        StageMemberLocked(room_id, player_id, member->second, writes);
        StageRoomStateLocked(room_id, outbox);
        EnqueueWritesLocked(writes);
      }
    }
    if (room_id.empty()) {
      Reject(player_id, RejectKind::kState, "already in a room");
    } else {
      Deliver(outbox);
    }
    return;
  }

  if (const auto* join = command.as_joinRoom_or_null()) {
    if (HasEmbeddedNul(join->roomId)) {
      Reject(player_id, RejectKind::kInvalid, "invalid room id");
      return;
    }
    Outbox outbox;
    Writes writes;
    bool joined = false;
    // The store answering "no such room" is the client's problem; the store
    // not answering is ours, and the two must not share a rejection kind — a
    // storage outage otherwise reads as a spike of desynced clients.
    bool store_answered = true;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!player_room_.contains(player_id)) {
        // The room may live on another instance (#1194 step 3): one
        // synchronous read materializes it here before refusing.
        if (!rooms_.contains(join->roomId)) {
          store_answered = RefreshRoomLocked(join->roomId, outbox);
        }
        const auto room = rooms_.find(join->roomId);
        if (room != rooms_.end()) {
          const auto [member, inserted] = room->second.members.emplace(player_id, Member{});
          LeaveWorldLocked(player_id, outbox);  // out of the plaza's world
          player_room_[player_id] = join->roomId;
          StageMemberLocked(join->roomId, player_id, member->second, writes);
          StageWakeLocked(join->roomId, writes);
          StageRoomStateLocked(join->roomId, outbox);
          EnqueueWritesLocked(writes);
          joined = true;
        }
      }
    }
    if (joined) {
      Deliver(outbox);
      // After the joiner's roomState, the chat they missed. Loaded
      // outside mu_; a message committing right now may appear in both
      // history and live delivery, which the model declares legal.
      SendChatHistory(join->roomId, player_id);
    } else {
      Reject(player_id, store_answered ? RejectKind::kState : RejectKind::kUnavailable,
             "room unavailable or already in a room");
    }
    return;
  }

  if (command.as_leaveRoom_or_null() != nullptr) {
    Outbox outbox;
    Writes writes;
    bool left = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto it = player_room_.find(player_id);
      if (it != player_room_.end()) {
        moonbase::games::RoomLeft ack;
        ack.roomId = it->second;
        outbox.To(player_id, GameEvents::FromRoomleft(std::move(ack)));
        LeaveEverywhere(player_id, outbox, writes);
        EnqueueWritesLocked(writes);
        left = true;
      }
    }
    if (left) {
      Deliver(outbox);
    } else {
      Reject(player_id, RejectKind::kState, "not in a room");
    }
    return;
  }

  if (command.as_getRoomState_or_null() != nullptr) {
    std::optional<GameEvents> snapshot;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto it = player_room_.find(player_id);
      if (it != player_room_.end()) {
        const auto room = rooms_.find(it->second);
        if (room != rooms_.end()) {
          snapshot = GameEvents::FromRoomstate(RoomStateLocked(it->second, room->second));
        }
      }
    }
    if (snapshot.has_value()) {
      Send(player_id, std::move(*snapshot));
    } else {
      Reject(player_id, RejectKind::kState, "not in a room");
    }
    return;
  }

  if (const auto* chat = command.as_chat_or_null()) {
    // One validation rule for the protocol edge and the stores, so a
    // client is told no for exactly the text a store would refuse.
    if (const absl::Status valid = ValidateChatText(chat->text); !valid.ok()) {
      Reject(player_id, RejectKind::kInvalid, std::string(valid.message()));
      return;
    }

    // Resolve the room, then drop the lock: MemoryChatStore re-takes it
    // through WithMember, and mu_ is not recursive. Membership is not
    // re-checked here either — the store's guard is that check, and it
    // holds the lock across the append so the answer cannot go stale.
    std::string room_id;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (const auto room = player_room_.find(player_id); room != player_room_.end()) {
        room_id = room->second;
      }
    }
    if (room_id.empty()) {
      Reject(player_id, RejectKind::kState, "not in a room");
      return;
    }

    const absl::StatusOr<ChatRow> appended =
        chat_store_->Append(room_id, player_id, chat->text, instance_id_);
    if (!appended.ok()) {
      // Nothing was stored, so nothing is echoed: the sender is told no
      // rather than shown a message no one else will ever receive.
      const bool stale = appended.status().code() == absl::StatusCode::kFailedPrecondition;
      Count("chat_appends", {{"result", stale ? "rejected" : "unavailable"}});
      Reject(player_id, stale ? RejectKind::kState : RejectKind::kUnavailable,
             stale ? "not in a room" : "chat is unavailable");
      return;
    }
    Count("chat_appends", {{"result", "stored"}});

    // The committed row reaches locals through the pump, like every
    // other row. That is not indirection for its own sake: a remote
    // append our commit raced past holds a lower id, and the pump's
    // cursor walk delivers it before ours — staging just our own row
    // and advancing the cursor over it would skip it for good.
    PumpChat(room_id);
    return;
  }

  if (const auto* golf_command = command.as_golf_or_null()) {
    HandleMove(player_id, golf_command->move);
    return;
  }

  if (const auto* castle_command = command.as_castle_or_null()) {
    HandleCastleMove(player_id, castle_command->move);
    return;
  }

  if (const auto* lobby = command.as_lobby_or_null()) {
    HandleLobby(player_id, lobby->action);
    return;
  }

  Reject(player_id, RejectKind::kUnknown, "unknown command");
}

void GolfHub::HandleLobby(const std::string& player_id,
                          const moonbase::games::LobbyAction& action) {
  std::optional<World::Refusal> refusal;
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    World::Deliveries deliveries;
    if (const auto* join = action.as_join_or_null()) {
      // The world is the session's: a roomId, if the client names one,
      // can only agree.
      const std::string world = WorldOfLocked(player_id);
      if (join->roomId.has_value() && *join->roomId != world) {
        refusal =
            World::Refusal{RejectKind::kState, "the world is your room's; join the room first"};
      } else {
        refusal = world_.Join(player_id, world, *join, deliveries);
      }
    } else if (const auto* move = action.as_move_or_null()) {
      refusal = world_.Move(player_id, *move, deliveries);
    } else if (const auto* shape = action.as_shape_or_null()) {
      refusal = world_.Shape(player_id, *shape, deliveries);
    } else if (action.as_leave_or_null() != nullptr) {
      if (!world_.Leave(player_id, deliveries)) {
        refusal = World::Refusal{RejectKind::kState, "not in the world"};
      }
    } else {
      refusal = World::Refusal{RejectKind::kUnknown, "unknown command"};
    }
    StageWorldLocked(deliveries, outbox);
  }
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

std::string GolfHub::WorldOfLocked(const std::string& player_id) const {
  const auto room = player_room_.find(player_id);
  return room != player_room_.end() ? room->second : std::string(World::kPlaza);
}

void GolfHub::StageWorldLocked(World::Deliveries& deliveries, Outbox& outbox) {
  for (auto& delivery : deliveries) {
    moonbase::games::LobbyEvent event;
    event.update = std::move(delivery.update);
    outbox.To(delivery.to, GameEvents::FromLobby(std::move(event)));
  }
  deliveries.clear();
}

void GolfHub::LeaveWorldLocked(const std::string& player_id, Outbox& outbox) {
  World::Deliveries deliveries;
  world_.Leave(player_id, deliveries);
  StageWorldLocked(deliveries, outbox);
}

void GolfHub::HandleMove(const std::string& player_id, const GolfMove& move) {
  if (move.as_createGame_or_null() != nullptr) {
    CreateGameMove(player_id, GameKind::kGolf);
    return;
  }
  if (const auto* join = move.as_joinGame_or_null()) {
    JoinGameMove(player_id, join->gameId, GameKind::kGolf);
    return;
  }
  if (move.as_startGame_or_null() != nullptr) {
    StartGameMove(player_id);
    return;
  }
  if (move.as_leaveGame_or_null() != nullptr) {
    Outbox outbox;
    Writes writes;
    bool in_game = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      in_game = player_game_.contains(player_id);
      if (in_game) LeaveGameLocked(player_id, outbox, writes);
      EnqueueWritesLocked(writes);
    }
    if (in_game) {
      Deliver(outbox);
    } else {
      Reject(player_id, RejectKind::kState, "not in a game");
    }
    return;
  }

  if (const auto* peek = move.as_peekCard_or_null()) {
    const auto position = golf::positionFromIndex(peek->cardIndex);
    if (!position.has_value()) {
      Reject(player_id, RejectKind::kInvalid, "invalid card index");
      return;
    }
    EngineMove(
        player_id,
        [position](const golf::GameState& state, int seat) {
          return state.peekOwnCard(seat, *position);
        },
        MoveEffects{.peek_fanout = true});
    return;
  }

  if (move.as_hideCards_or_null() != nullptr) {
    EngineMove(
        player_id, [](const golf::GameState& state, int seat) { return state.hideCards(seat); },
        MoveEffects{});
    return;
  }

  if (move.as_drawCard_or_null() != nullptr) {
    // The wire's drawCard is the engine's draw-pile peek — unrelated to
    // peekCard, which is the opening own-card reveal.
    EngineMove(
        player_id,
        [](const golf::GameState& state, int seat) { return state.peekAtDrawPile(seat); },
        MoveEffects{});
    return;
  }

  if (const auto* take = move.as_takeFromDiscard_or_null()) {
    const auto position = golf::positionFromIndex(take->cardIndex);
    if (!position.has_value()) {
      Reject(player_id, RejectKind::kInvalid, "invalid card index");
      return;
    }
    EngineMove(
        player_id,
        [position](const golf::GameState& state, int seat) {
          return state.swapForDiscardPile(seat, *position);
        },
        MoveEffects{.announce_turn = true});
    return;
  }

  if (const auto* swap = move.as_swapCard_or_null()) {
    const auto position = golf::positionFromIndex(swap->cardIndex);
    if (!position.has_value()) {
      Reject(player_id, RejectKind::kInvalid, "invalid card index");
      return;
    }
    EngineMove(
        player_id,
        [position](const golf::GameState& state, int seat) {
          return state.swapForDrawPile(seat, *position);
        },
        MoveEffects{.announce_turn = true});
    return;
  }

  if (move.as_discardDrawn_or_null() != nullptr) {
    EngineMove(
        player_id,
        [](const golf::GameState& state, int seat) { return state.swapDrawForDiscardPile(seat); },
        MoveEffects{.announce_turn = true});
    return;
  }

  if (move.as_knock_or_null() != nullptr) {
    EngineMove(
        player_id, [](const golf::GameState& state, int seat) { return state.knock(seat); },
        MoveEffects{.announce_knock = true});
    return;
  }

  Reject(player_id, RejectKind::kUnknown, "unknown move");
}

void GolfHub::HandleCastleMove(const std::string& player_id, const CastleMove& move) {
  if (move.as_createGame_or_null() != nullptr) {
    CreateGameMove(player_id, GameKind::kCastle);
    return;
  }
  if (const auto* join = move.as_joinGame_or_null()) {
    JoinGameMove(player_id, join->gameId, GameKind::kCastle);
    return;
  }
  if (move.as_startGame_or_null() != nullptr) {
    StartGameMove(player_id);
    return;
  }
  if (move.as_leaveGame_or_null() != nullptr) {
    Outbox outbox;
    Writes writes;
    bool in_game = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      in_game = player_game_.contains(player_id);
      if (in_game) LeaveGameLocked(player_id, outbox, writes);
      EnqueueWritesLocked(writes);
    }
    if (in_game) {
      Deliver(outbox);
    } else {
      Reject(player_id, RejectKind::kState, "not in a game");
    }
    return;
  }

  if (const auto* swap = move.as_swapForSetup_or_null()) {
    const int hand_index = swap->handIndex;
    const int face_up_index = swap->faceUpIndex;
    CastleEngineMove(player_id,
                     [hand_index, face_up_index](const castle::GameState& state, int seat) {
                       return state.swapForSetup(seat, hand_index, face_up_index);
                     });
    return;
  }
  if (move.as_ready_or_null() != nullptr) {
    CastleEngineMove(player_id,
                     [](const castle::GameState& state, int seat) { return state.ready(seat); });
    return;
  }
  if (const auto* play = move.as_playFromHand_or_null()) {
    const std::vector<int> indexes = play->indexes;
    CastleEngineMove(player_id, [indexes](const castle::GameState& state, int seat) {
      return state.playFromHand(seat, indexes);
    });
    return;
  }
  if (const auto* play = move.as_playFaceUp_or_null()) {
    const std::vector<int> indexes = play->indexes;
    CastleEngineMove(player_id, [indexes](const castle::GameState& state, int seat) {
      return state.playFaceUp(seat, indexes);
    });
    return;
  }
  if (const auto* play = move.as_playFaceDown_or_null()) {
    const int index = play->index;
    CastleEngineMove(player_id, [index](const castle::GameState& state, int seat) {
      return state.playFaceDown(seat, index);
    });
    return;
  }
  if (move.as_pickUp_or_null() != nullptr) {
    CastleEngineMove(player_id,
                     [](const castle::GameState& state, int seat) { return state.pickUp(seat); });
    return;
  }

  Reject(player_id, RejectKind::kUnknown, "unknown move");
}

void GolfHub::CreateGameMove(const std::string& player_id, GameKind kind) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    Room* room = FindRoomLocked(player_id);
    if (room == nullptr) {
      refusal = Refusal{RejectKind::kState, "not in a room"};
    } else if (player_game_.contains(player_id)) {
      refusal = Refusal{RejectKind::kState, "leave your current game first"};
    } else {
      const std::string room_id = player_room_.at(player_id);
      for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
        std::string game_id = ids_->GameCode();
        while (room->games.contains(game_id)) game_id = ids_->GameCode();
        GameEntry& entry = room->games[game_id];
        entry.kind = kind;
        const Commit commit =
            CommitEntryLocked(room_id, game_id, entry, {player_id}, std::nullopt, nullptr);
        if (commit == Commit::kRebased)
          continue;  // code taken remotely; the
                     // adopted game is real — keep
                     // it and roll another code
        if (commit == Commit::kGone) {
          room->games.erase(game_id);
          continue;
        }
        if (commit == Commit::kUnavailable) {
          room->games.erase(game_id);
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        player_game_[player_id] = game_id;

        moonbase::games::GameCreated announcement;
        announcement.gameId = game_id;
        announcement.createdBy = player_id;
        for (const auto& member : room->members) {
          outbox.To(member.first, CreatedEvent(kind, announcement));
        }
        outbox.To(player_id, JoinedEventLocked(game_id, entry, player_id));
        StageRoomStateLocked(room_id, outbox);
        break;
      }
      if (!refusal.has_value() && !player_game_.contains(player_id)) {
        refusal = Refusal{RejectKind::kUnavailable, "could not allocate a game code; try again"};
      }
    }
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
  } else {
    Deliver(outbox);
  }
}

void GolfHub::JoinGameMove(const std::string& player_id, const std::string& game_id,
                           GameKind kind) {
  if (HasEmbeddedNul(game_id)) {
    Reject(player_id, RejectKind::kInvalid, "invalid game id");
    return;
  }
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    Room* room = FindRoomLocked(player_id);
    if (room == nullptr) {
      refusal = Refusal{RejectKind::kState, "not in a room"};
    } else if (player_game_.contains(player_id)) {
      refusal = Refusal{RejectKind::kState, "leave your current game first"};
    } else {
      const std::string room_id = player_room_.at(player_id);
      bool store_answered = true;
      if (!room->games.contains(game_id)) {
        // Another instance may have created it since our last wake.
        store_answered = RefreshRoomLocked(room_id, outbox);
        room = FindRoomLocked(player_id);  // the refresh can drop us or the room
      }
      if (room == nullptr || !room->games.contains(game_id)) {
        // An unanswered refresh is an outage, not a missing game — see
        // RefreshRoomLocked. The commit path below reports its own outages.
        refusal = store_answered
                      ? Refusal{RejectKind::kState, "game not found"}
                      : Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
      } else {
        for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
          GameEntry& entry = room->games.at(game_id);
          if (entry.kind != kind) {
            refusal = Refusal{RejectKind::kState,
                              absl::StrCat("that table plays ", GameKindName(entry.kind))};
            break;
          }
          if (entry.started()) {
            refusal = Refusal{RejectKind::kState, "game already started"};
            break;
          }
          if (entry.roster.size() >= kMaxSeats) {
            refusal = Refusal{RejectKind::kState, "game is full"};
            break;
          }
          std::vector<std::string> roster = entry.roster;
          roster.push_back(player_id);
          const Commit commit =
              CommitEntryLocked(room_id, game_id, entry, roster, std::nullopt, nullptr);
          if (commit == Commit::kRebased) continue;
          if (commit == Commit::kGone) {
            room->games.erase(game_id);
            refusal = Refusal{RejectKind::kState, "game not found"};
            break;
          }
          if (commit == Commit::kUnavailable) {
            refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
            break;
          }
          player_game_[player_id] = game_id;

          outbox.To(player_id, JoinedEventLocked(game_id, entry, player_id));
          Outbox others;
          StageGameViewsLocked(game_id, entry, others);
          for (auto& [recipient, event] : others.events) {
            if (recipient != player_id) outbox.To(recipient, std::move(event));
          }
          StageRoomStateLocked(room_id, outbox);
          break;
        }
        if (!refusal.has_value() && !player_game_.contains(player_id)) {
          refusal = Refusal{RejectKind::kState, "game changed; try again"};
        }
      }
    }
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
  } else {
    Deliver(outbox);
  }
}

void GolfHub::StartGameMove(const std::string& player_id) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      refusal = Refusal{RejectKind::kState, "not in a game"};
    } else {
      bool started = false;
      for (int attempt = 0; attempt < kMaxCommitAttempts && !refusal.has_value(); ++attempt) {
        if (ref->entry->started()) {
          refusal = Refusal{RejectKind::kState, "game already started"};
          break;
        }
        if (ref->entry->roster.size() < 2) {
          refusal = Refusal{RejectKind::kState, "need at least 2 players to start"};
          break;
        }
        std::deque<cards::Card> deck = dealer_->DealNewUnshuffledDeck();
        dealer_->ShuffleDeck(deck);
        std::optional<HostedState> dealt;
        if (ref->entry->kind == GameKind::kCastle) {
          auto castle_deal =
              castle::dealCastleGame(ref->game_id, ref->entry->roster, std::move(deck));
          if (!castle_deal.ok()) {
            refusal = Refusal{RejectKind::kRules, std::string(castle_deal.status().message())};
            break;
          }
          dealt.emplace(*std::move(castle_deal));
        } else {
          auto golf_deal = golf::dealGolfGame(ref->game_id, ref->entry->roster, std::move(deck));
          if (!golf_deal.ok()) {
            refusal = Refusal{RejectKind::kRules, std::string(golf_deal.status().message())};
            break;
          }
          dealt.emplace(*std::move(golf_deal));
        }
        const Commit commit = CommitEntryLocked(ref->room_id, ref->game_id, *ref->entry,
                                                ref->entry->roster, dealt, nullptr);
        if (commit == Commit::kRebased) continue;  // the roster (or starter) raced us; redeal
        if (commit == Commit::kGone) {
          DropGameLocked(*ref);
          StageRoomStateLocked(ref->room_id, outbox);
          refusal = Refusal{RejectKind::kState, "game no longer exists"};
          break;
        }
        if (commit == Commit::kUnavailable) {
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        started = true;
        for (const std::string& recipient : ref->entry->roster) {
          outbox.To(recipient, StartedEvent(ref->entry->kind));
        }
        StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
        StageRoomStateLocked(ref->room_id, outbox);
        break;
      }
      if (!refusal.has_value() && !started) {
        refusal = Refusal{RejectKind::kState, "game changed; try again"};
      }
    }
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
  } else {
    Deliver(outbox);
  }
}

void GolfHub::EngineMove(const std::string& player_id, const MoveFn& move, MoveEffects effects) {
  Outbox outbox;
  Writes writes;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      refusal = Refusal{RejectKind::kState, "not in a game"};
    } else if (!ref->entry->started()) {
      refusal = Refusal{RejectKind::kState, "game not started"};
    } else {
      bool landed = false;
      // The issue's move loop: pure transition off the entry, then the
      // conditional commit; a miss adopts the stored truth and replays
      // the transition against it. The kind is re-read each attempt: a
      // rebase adopts whatever the store holds under this code.
      for (int attempt = 0; attempt < kMaxCommitAttempts && !refusal.has_value(); ++attempt) {
        if (ref->entry->kind != GameKind::kGolf) {
          refusal = Refusal{RejectKind::kState, "that table plays castle"};
          break;
        }
        const golf::GameState& state = ref->entry->golf();
        const int seat = state.playerIndex(player_id);
        if (seat < 0) {
          refusal = Refusal{RejectKind::kState, "not seated in this game"};
          break;
        }
        auto next = move(state, seat);
        if (!next.ok()) {
          // The engine said no: a rules refusal, not a desync.
          refusal = Refusal{RejectKind::kRules, std::string(next.status().message())};
          break;
        }
        const bool was_countdown = state.revealCountdownActive();
        // Compare occupant ids, not seat indexes — a mid-round leave
        // renumbers seats.
        const std::string previous_turn =
            effects.announce_turn ? PlayerIdAt(state, state.getWhoseTurn()) : std::string();
        const bool over = next->isOver();
        std::vector<HubStore::StatsDelta> deltas;
        if (over) deltas = StatsDeltas(*next, ref->entry->roster);
        const Commit commit =
            CommitEntryLocked(ref->room_id, ref->game_id, *ref->entry, ref->entry->roster,
                              HostedState(*std::move(next)), over ? &deltas : nullptr);
        if (commit == Commit::kRebased) continue;  // another instance moved first
        if (commit == Commit::kGone) {
          DropGameLocked(*ref);
          StageRoomStateLocked(ref->room_id, outbox);
          refusal = Refusal{RejectKind::kState, "game no longer exists"};
          break;
        }
        if (commit == Commit::kUnavailable) {
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        landed = true;
        const golf::GameState& updated = ref->entry->golf();

        if (effects.announce_knock) {
          moonbase::games::PlayerKnocked knocked;
          knocked.playerId = player_id;
          for (const std::string& recipient : ref->entry->roster) {
            outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromPlayerknocked(knocked)));
          }
        }

        if (over) {
          // Finalize stages the definitive face-up views itself.
          FinalizeGameLocked(ref->room_id, *ref->room, ref->game_id, outbox);
        } else {
          const bool countdown_started = !was_countdown && updated.revealCountdownActive();
          if (effects.peek_fanout && !countdown_started) {
            // A quiet peek: only the peeker's view changed.
            moonbase::games::GameStateUpdate update;
            update.view = ViewLocked(ref->game_id, *ref->entry, player_id);
            outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
          } else {
            StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
          }
          if (effects.announce_turn) {
            const std::string current_turn = PlayerIdAt(updated, updated.getWhoseTurn());
            if (current_turn != previous_turn) {
              moonbase::games::TurnChanged turn;
              turn.playerId = current_turn;
              for (const std::string& recipient : ref->entry->roster) {
                outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromTurnchanged(turn)));
              }
            }
          }
        }
        break;
      }
      if (!refusal.has_value() && !landed) {
        refusal = Refusal{RejectKind::kState, "game changed; try again"};
      }
    }
    EnqueueWritesLocked(writes);
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
  } else {
    Deliver(outbox);
  }
}

void GolfHub::CastleEngineMove(const std::string& player_id, const CastleMoveFn& move) {
  Outbox outbox;
  Writes writes;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      refusal = Refusal{RejectKind::kState, "not in a game"};
    } else if (!ref->entry->started()) {
      refusal = Refusal{RejectKind::kState, "game not started"};
    } else {
      bool landed = false;
      for (int attempt = 0; attempt < kMaxCommitAttempts && !refusal.has_value(); ++attempt) {
        if (ref->entry->kind != GameKind::kCastle) {
          refusal = Refusal{RejectKind::kState, "that table plays golf"};
          break;
        }
        const castle::GameState& state = ref->entry->castle();
        const int seat = state.playerIndex(player_id);
        if (seat < 0) {
          refusal = Refusal{RejectKind::kState, "not seated in this game"};
          break;
        }
        auto next = move(state, seat);
        if (!next.ok()) {
          refusal = Refusal{RejectKind::kRules, std::string(next.status().message())};
          break;
        }
        // Occupant ids, not seats: the engine renumbers on a leave. The
        // opening turn (setup done) reads as a change from nobody.
        const std::string previous_turn = CurrentTurnOf(*ref->entry->state);
        const bool over = next->isOver();
        std::vector<HubStore::StatsDelta> deltas;
        if (over) deltas = CastleStatsDeltas(*next);
        const Commit commit =
            CommitEntryLocked(ref->room_id, ref->game_id, *ref->entry, ref->entry->roster,
                              HostedState(*std::move(next)), over ? &deltas : nullptr);
        if (commit == Commit::kRebased) continue;
        if (commit == Commit::kGone) {
          DropGameLocked(*ref);
          StageRoomStateLocked(ref->room_id, outbox);
          refusal = Refusal{RejectKind::kState, "game no longer exists"};
          break;
        }
        if (commit == Commit::kUnavailable) {
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        landed = true;
        if (over) {
          FinalizeGameLocked(ref->room_id, *ref->room, ref->game_id, outbox);
        } else {
          StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
          const std::string current_turn = CurrentTurnOf(*ref->entry->state);
          if (current_turn != previous_turn) {
            moonbase::games::TurnChanged turn;
            turn.playerId = current_turn;
            for (const std::string& recipient : ref->entry->roster) {
              outbox.To(recipient, TurnEvent(GameKind::kCastle, turn));
            }
          }
        }
        break;
      }
      if (!refusal.has_value() && !landed) {
        refusal = Refusal{RejectKind::kState, "game changed; try again"};
      }
    }
    EnqueueWritesLocked(writes);
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
  } else {
    Deliver(outbox);
  }
}

void GolfHub::SetConnected(const std::string& player_id, bool connected) {
  Writes writes;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const auto it = player_room_.find(player_id);
    if (it == player_room_.end()) return;
    const auto room = rooms_.find(it->second);
    if (room == rooms_.end()) return;
    const auto member = room->second.members.find(player_id);
    if (member == room->second.members.end()) return;
    member->second.connected = connected;
    StageMemberLocked(it->second, player_id, member->second, writes);
    StageWakeLocked(it->second, writes);
    EnqueueWritesLocked(writes);
  }
}

std::optional<std::string> GolfHub::CurrentRoom(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return std::nullopt;
  return it->second;
}

bool GolfHub::WithMember(const std::string& room_id, const std::string& player_id,
                         const MemberAction& action) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return false;
  if (room->second.members.count(player_id) == 0) return false;
  action();
  return true;
}

GolfHub::Room* GolfHub::FindRoomLocked(const std::string& player_id) {
  const auto room_it = player_room_.find(player_id);
  if (room_it == player_room_.end()) return nullptr;
  const auto room = rooms_.find(room_it->second);
  return room != rooms_.end() ? &room->second : nullptr;
}

std::optional<GolfHub::GameRef> GolfHub::FindGameLocked(const std::string& player_id) {
  const auto room_it = player_room_.find(player_id);
  const auto game_it = player_game_.find(player_id);
  if (room_it == player_room_.end() || game_it == player_game_.end()) return std::nullopt;
  const auto room = rooms_.find(room_it->second);
  if (room == rooms_.end()) return std::nullopt;
  const auto game = room->second.games.find(game_it->second);
  if (game == room->second.games.end()) return std::nullopt;
  return GameRef{room_it->second, &room->second, game_it->second, &game->second};
}

void GolfHub::LeaveEverywhere(const std::string& player_id, Outbox& outbox, Writes& writes) {
  LeaveWorldLocked(player_id, outbox);
  LeaveGameLocked(player_id, outbox, writes);

  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return;
  const std::string room_id = it->second;
  player_room_.erase(it);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return;
  room->second.members.erase(player_id);
  if (room->second.members.empty()) {
    rooms_.erase(room);
    // Chat dies with its room. PostgreSQL gets this from the cascade on
    // the DeleteRoom below, but MemoryChatStore reclaims only here, and
    // it is what production runs today — without this an emptied room
    // keeps its last hundred messages for the life of the process.
    chat_store_->DropRoom(room_id);
    chat_cursors_.erase(room_id);
    // One DeleteRoom; the row's cascade takes members and games with it.
    // The wake rider tells any instance that still holds the room (a
    // race, not the norm — an emptied room has no members anywhere).
    StageLocked(writes, HubStore::DeleteRoom{room_id});
    StageWakeLocked(room_id, writes);
    UnlistenRoomLocked(room_id);
    return;  // nobody left to tell
  }
  StageLocked(writes, HubStore::DeleteMember{room_id, player_id});
  StageWakeLocked(room_id, writes);
  StageRoomStateLocked(room_id, outbox);
}

void GolfHub::LeaveGameLocked(const std::string& player_id, Outbox& outbox, Writes& writes) {
  auto ref = FindGameLocked(player_id);
  player_game_.erase(player_id);
  if (!ref.has_value()) return;

  moonbase::games::GameLeft ack;
  ack.gameId = ref->game_id;
  outbox.To(player_id, LeftEvent(ref->entry->kind, std::move(ack)));

  for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
    GameEntry& entry = *ref->entry;
    if (std::find(entry.roster.begin(), entry.roster.end(), player_id) == entry.roster.end()) {
      // A rebase says we are already out (another instance's view of the
      // roster caught up first); nothing left to commit.
      StageRoomStateLocked(ref->room_id, outbox);
      return;
    }
    std::vector<std::string> roster = entry.roster;
    roster.erase(std::remove(roster.begin(), roster.end(), player_id), roster.end());

    if (!entry.started() && roster.empty()) {
      ref->room->games.erase(ref->game_id);
      StageLocked(writes, HubStore::DeleteGame{ref->room_id, ref->game_id});
      StageWakeLocked(ref->room_id, writes);
      StageRoomStateLocked(ref->room_id, outbox);
      return;
    }

    // With seats to spare the game continues compacted; otherwise the
    // engine ends it — golf keeps every seat so the departed hand still
    // reaches the scorecard (#1236), castle abandons with no loser. The
    // shrunken roster is what records who left — and who can still win.
    const std::optional<HostedState> state =
        entry.started() ? WithoutSeat(*entry.state, player_id) : std::nullopt;
    const std::string previous_turn = entry.started() ? CurrentTurnOf(*entry.state) : "";
    const bool over = state.has_value() && IsOver(*state);
    std::vector<HubStore::StatsDelta> deltas;
    if (over) deltas = StatsDeltasOf(*state, roster);
    const Commit commit = CommitEntryLocked(ref->room_id, ref->game_id, entry, roster, state,
                                            over ? &deltas : nullptr);
    if (commit == Commit::kRebased) continue;
    if (commit == Commit::kGone) {
      DropGameLocked(*ref);
      StageRoomStateLocked(ref->room_id, outbox);
      return;
    }
    if (commit == Commit::kUnavailable) {
      // A disconnect cannot be refused: the seat empties either way.
      // Adopt locally and let the next successful commit's rebase heal
      // the divergence. Loud — rosters disagree until then.
      LOG(ERROR) << "game " << ref->room_id << "/" << ref->game_id
                 << ": leave did not commit; applying locally only";
      entry.roster = roster;
      if (state.has_value()) entry.state.emplace(*state);
      ++entry.version;
    }
    if (over) {
      // Finalize stages the final views and the room's refreshed stats.
      FinalizeGameLocked(ref->room_id, *ref->room, ref->game_id, outbox);
    } else {
      StageGameViewsLocked(ref->game_id, entry, outbox);
      // The leaver may have held the turn, or been the seat setup was
      // waiting on: whoever plays next hears it the way a move says it.
      if (entry.started()) {
        const std::string current_turn = CurrentTurnOf(*entry.state);
        if (current_turn != previous_turn) {
          moonbase::games::TurnChanged turn;
          turn.playerId = current_turn;
          for (const std::string& recipient : entry.roster) {
            outbox.To(recipient, TurnEvent(entry.kind, turn));
          }
        }
      }
      StageRoomStateLocked(ref->room_id, outbox);
    }
    return;
  }
  // Three straight rebases: the entry now mirrors the store; the caller
  // (disconnect or a retried leave) comes around again.
  LOG(WARNING) << "game " << ref->room_id << "/" << ref->game_id
               << ": leave lost every commit race";
}

void GolfHub::BroadcastRoom(const std::string& room_id) {
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    StageRoomStateLocked(room_id, outbox);
  }
  Deliver(outbox);
}

void GolfHub::Reject(const std::string& player_id, Refusal refusal) {
  Reject(player_id, refusal.kind, std::move(refusal.reason));
}

void GolfHub::Reject(const std::string& player_id, RejectKind kind, std::string reason) {
  // The bounded kind is the metric; the free text goes only to the player.
  // Reasons come from ~30 literals here and the cards engine's status
  // messages, exactly the label set that cannot be declared and so would
  // reopen #1323 for the rejection someone is actually looking for.
  Count("golf_rejections", {{"kind", RejectKindName(kind)}});
  moonbase::games::CommandRejected rejected;
  rejected.reason = std::move(reason);
  Send(player_id, GameEvents::FromCommandrejected(std::move(rejected)));
}

void GolfHub::OnExpired(const std::string& player_id) {
  // Grace ran out (ADR-0020): the seat is gone; free the room and game
  // slots and tell whoever remains. Runs on the registry's expiry thread.
  Count("golf_seats_expired");
  ReapUnlessResumedElsewhere(player_id);
}

bool GolfHub::ReapUnlessResumedElsewhere(const std::string& player_id) {
  Outbox outbox;
  Writes writes;
  bool reaped = false;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // Cross-instance grace (#1194 step 3): the player may have resumed
    // on another instance while parked here. One fresh read decides —
    // a member row back at connected belongs to its new instance. The
    // boot reaper leans on the same check from the other side: a row
    // still at connected after a whole boot grace may be a sibling's
    // live seat, so it stays (#1295 records the orphaned-row residue
    // this leaves — only that seat's owner could have known better).
    bool resumed_elsewhere = false;
    if (const auto room_it = player_room_.find(player_id); room_it != player_room_.end()) {
      RefreshRoomLocked(room_it->second, outbox);
      if (Room* room = FindRoomLocked(player_id); room != nullptr) {
        const auto member = room->members.find(player_id);
        resumed_elsewhere = member != room->members.end() && member->second.connected;
      }
    }
    if (!resumed_elsewhere) {
      LeaveEverywhere(player_id, outbox, writes);
      EnqueueWritesLocked(writes);
      reaped = true;
    }
  }
  Deliver(outbox);
  return reaped;
}

void GolfHub::Deliver(Outbox& outbox) {
  for (auto& [player_id, event] : outbox.events) {
    Send(player_id, std::move(event));
  }
  outbox.events.clear();
}

void GolfHub::PumpChat(const std::string& room_id) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // No cursor means no room (they live and die together): a stale wake
    // must not resurrect anything.
    const auto cursor = chat_cursors_.find(room_id);
    if (cursor == chat_cursors_.end()) return;
    if (cursor->second.pumping) {
      // One drain at a time; the drainer loops once more for us.
      cursor->second.again = true;
      return;
    }
    cursor->second.pumping = true;
  }

  bool more = true;
  // Rows this drain staged, across pages and again-loops. Counted when the
  // drain ends as one drain plus its rows — two counters rather than a
  // histogram, because a counter can be declared at zero and a histogram
  // cannot without biasing its mean (#1384). The dashboard's lag signal,
  // rows per drain, is the rate ratio of the two, with no room id attached.
  int64_t drained = 0;
  while (more) {
    int64_t after = 0;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto cursor = chat_cursors_.find(room_id);
      if (cursor == chat_cursors_.end()) return;  // room dropped mid-pump
      after = cursor->second.delivered;
    }

    // Outside mu_: the load may reach a database.
    auto rows = chat_store_->LoadAfter(room_id, after, kChatCatchUpPage);

    Outbox outbox;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto cursor = chat_cursors_.find(room_id);
      const auto room = rooms_.find(room_id);
      if (cursor == chat_cursors_.end() || room == rooms_.end()) return;
      if (!rows.ok()) {
        Count("chat_failures", {{"stage", "catch_up"}});
        LOG(WARNING) << "chat catch-up load failed: " << rows.status();
        if (cursor->second.again) {
          // A wake landed while this load was failing. That signal may
          // be the only one an already-committed append gets — its own
          // pump call already came and coalesced — so it buys one
          // immediate retry instead of being thrown away. Bounded: each
          // retry consumes a signal, so a persistent outage still exits.
          cursor->second.again = false;
          continue;
        }
        cursor->second.pumping = false;
        break;  // ends the drain; the per-drain counters below still record
      }
      for (const ChatRow& row : *rows) {
        if (row.message_id <= cursor->second.delivered) continue;
        const GameEvents event = GameEvents::FromRoomchat(ChatEvent(row));
        for (const auto& member : room->second.members) outbox.To(member.first, event);
        cursor->second.delivered = row.message_id;
        ++drained;
      }
      more = rows->size() == kChatCatchUpPage;
      if (!more && cursor->second.again) {
        cursor->second.again = false;
        more = true;
      }
      if (!more) cursor->second.pumping = false;
    }
    Deliver(outbox);
  }
  if (metrics_ != nullptr) {
    if (drained > 0) metrics_->RecordCounter("chat_rows_delivered", drained);
    // Every drain, including one that found nothing: the zero drains are
    // what keep the windowed rows-per-drain average honest.
    metrics_->RecordCounter("chat_catch_up_drains");
  }
}

void GolfHub::SendChatHistory(const std::string& room_id, const std::string& player_id) {
  auto rows = chat_store_->LoadRecent(room_id, kChatHistoryLimit);
  if (!rows.ok()) {
    // The admission already succeeded; chat history is not worth failing
    // it over. No room id or text in the log — the status is enough.
    Count("chat_failures", {{"stage", "history_load"}});
    LOG(WARNING) << "chat history load failed for a joining stream: " << rows.status();
    return;
  }
  // No cursor work here: the room's cursor predates every join (born
  // with the room under mu_), and a replay ending behind it or ahead of
  // it just overlaps live delivery, which the model declares legal.
  // Sent even when empty: a stream that entered a room always hears one
  // history event, so the client has a deterministic signal rather than
  // inferring emptiness from absence.
  moonbase::games::ChatHistory history;
  history.messages.reserve(rows->size());
  for (const ChatRow& row : *rows) history.messages.push_back(ChatEvent(row));
  Send(player_id, GameEvents::FromRoomchathistory(std::move(history)));
  Count("chat_history_replays");
}

void GolfHub::Count(const char* name, const std::map<std::string, std::string>& attributes) {
  if (metrics_) metrics_->RecordCounter(name, 1, attributes);
}

void GolfHub::TrackActive(int delta) {
  // Delta form, matching http_server_requests_active: the collector sums
  // an up-down counter into the live-session count.
  if (metrics_) metrics_->RecordGauge("golf_sessions_active", delta);
}

// Each tenant's envelope counts on its own series (castle_*, lobby_*);
// the room layer's commands and events stay on golf_* (the stream's
// original name).
void GolfHub::CountCommand(const GameCommands& command) {
  if (!metrics_) return;
  if (const auto* castle_envelope = command.as_castle_or_null()) {
    metrics_->RecordCounter("castle_commands", 1,
                            {{"command", std::string(castle_envelope->move.case_name())}});
    return;
  }
  if (const auto* lobby = command.as_lobby_or_null()) {
    metrics_->RecordCounter("lobby_commands", 1,
                            {{"command", std::string(lobby->action.case_name())}});
    return;
  }
  const auto* envelope = command.as_golf_or_null();
  const std::string name = envelope != nullptr ? absl::StrCat("golf.", envelope->move.case_name())
                                               : std::string(command.case_name());
  metrics_->RecordCounter("golf_commands", 1, {{"command", name}});
}

void GolfHub::Send(const std::string& player_id, GameEvents event) {
  if (metrics_) {
    if (const auto* castle_envelope = event.as_castle_or_null()) {
      metrics_->RecordCounter("castle_events", 1,
                              {{"event", std::string(castle_envelope->update.case_name())}});
    } else {
      if (const auto* lobby = event.as_lobby_or_null()) {
        metrics_->RecordCounter("lobby_events", 1,
                                {{"event", std::string(lobby->update.case_name())}});
      } else {
        const auto* envelope = event.as_golf_or_null();
        const std::string name = envelope != nullptr
                                     ? absl::StrCat("golf.", envelope->update.case_name())
                                     : std::string(event.case_name());
        metrics_->RecordCounter("golf_events", 1, {{"event", name}});
      }
    }
  }
  registry_.SendTo(player_id, std::move(event));
}

moonbase::games::RoomState GolfHub::RoomStateLocked(const std::string& room_id,
                                                    const Room& room) const {
  moonbase::games::RoomState state;
  state.roomId = room_id;
  for (const auto& [member_id, member] : room.members) {
    moonbase::games::PlayerInfo info;
    info.playerId = member_id;
    info.connected = member.connected;
    info.gamesPlayed = member.games_played;
    info.gamesWon = member.games_won;
    info.totalScore = member.total_score;
    // The table is the roster: a finished table leaves the map with its
    // ceremony (ReconcileRoomLocked pays one a rebase left owing), so
    // anyone listed is at a live one, pending or in play.
    for (const auto& [game_id, entry] : room.games) {
      if (std::find(entry.roster.begin(), entry.roster.end(), member_id) != entry.roster.end()) {
        moonbase::games::Table table;
        table.game = std::string(GameKindName(entry.kind));
        table.gameId = game_id;
        info.table = std::move(table);
        break;
      }
    }
    state.players.push_back(std::move(info));
  }
  for (const auto& [game_id, entry] : room.games) {
    moonbase::games::GameSummary summary;
    summary.gameId = game_id;
    summary.game = std::string(GameKindName(entry.kind));
    summary.status = entry.started() ? PhaseStringOf(*entry.state) : "waiting";
    summary.playerCount = static_cast<int>(entry.roster.size());
    state.games.push_back(std::move(summary));
  }
  return state;
}

void GolfHub::StageRoomStateLocked(const std::string& room_id, Outbox& outbox) const {
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return;
  const moonbase::games::RoomState state = RoomStateLocked(room_id, room->second);
  for (const auto& member : room->second.members) {
    outbox.To(member.first, GameEvents::FromRoomstate(state));
  }
}

moonbase::games::GameView GolfHub::ViewLocked(const std::string& game_id, const GameEntry& entry,
                                              const std::string& viewer_id) const {
  moonbase::games::GameView view;
  view.gameId = game_id;

  if (!entry.started()) {
    view.phase = "waiting";
    view.drawPileCount = 0;
    view.discardCount = 0;
    view.allPlayersPeeked = false;
    for (const std::string& roster_id : entry.roster) {
      moonbase::games::GamePlayer player;
      player.playerId = roster_id;
      player.cards.resize(4);
      player.hasPeeked = false;
      view.players.push_back(std::move(player));
    }
    return view;
  }

  const golf::GameState& state = entry.golf();
  const bool ended = state.isOver();
  view.phase = PhaseString(state);
  if (!ended) view.currentPlayerId = PlayerIdAt(state, state.getWhoseTurn());
  view.drawPileCount = static_cast<int>(state.getDrawPile().size());
  view.discardCount = static_cast<int>(state.getDiscardPile().size());
  if (!state.getDiscardPile().empty()) view.discardTop = WireCard(state.getDiscardPile().back());
  // Only a real seat's knock is advertised: an abandonment end carries
  // the kAbandoned sentinel, not a knock somebody would be blamed for.
  if (state.getWhoKnocked() >= 0) view.knockedPlayerId = PlayerIdAt(state, state.getWhoKnocked());
  view.allPlayersPeeked = state.allPlayersPeeked();
  // The drawn card rides only to the player who is looking at it.
  if (state.getPeekedAtDrawPile() && !ended &&
      PlayerIdAt(state, state.getWhoseTurn()) == viewer_id && !state.getDrawPile().empty()) {
    view.drawnCard = WireCard(state.getDrawPile().back());
  }

  for (const golf::Player& seat : state.getPlayers()) {
    const std::string occupant = seat.getName().value_or("");
    moonbase::games::GamePlayer player;
    player.playerId = occupant;
    player.cards.resize(4);
    player.hasPeeked = seat.hasCompletedPeeks();
    if (ended) {
      const std::vector<cards::Card> hand = seat.allCards();
      for (int i = 0; i < 4; ++i) {
        player.cards[i].card = WireCard(hand[static_cast<std::size_t>(i)]);
        player.revealedIndexes.push_back(i);
      }
      player.score = seat.score();
    } else if (occupant == viewer_id) {
      for (const golf::Position position : seat.getPeeked()) {
        const int index = golf::indexOfPosition(position);
        player.cards[static_cast<std::size_t>(index)].card = WireCard(seat.cardAt(position));
        player.revealedIndexes.push_back(index);
      }
    }
    view.players.push_back(std::move(player));
  }
  return view;
}

moonbase::games::CastleView GolfHub::CastleViewLocked(const std::string& game_id,
                                                      const GameEntry& entry,
                                                      const std::string& viewer_id) const {
  moonbase::games::CastleView view;
  view.gameId = game_id;
  if (!entry.started()) {
    view.phase = "waiting";
    view.drawPileCount = 0;
    view.pileCount = 0;
    for (const std::string& roster_id : entry.roster) {
      moonbase::games::CastlePlayer player;
      player.playerId = roster_id;
      player.ready = false;
      player.handCount = 0;
      player.faceDownCount = 0;
      player.out = false;
      view.players.push_back(std::move(player));
    }
    return view;
  }

  const castle::GameState& state = entry.castle();
  const bool ended = state.isOver();
  view.phase = CastlePhaseString(state);
  const std::string current = CurrentTurnOf(*entry.state);
  if (!ended && !current.empty()) view.currentPlayerId = current;
  view.drawPileCount = static_cast<int>(state.getDrawPile().size());
  view.pileCount = static_cast<int>(state.getPile().size());
  // The run on top, top last: the price of the next play, and the cards
  // a table shows.
  const std::vector<cards::Card>& pile = state.getPile();
  for (std::size_t i = pile.size() - state.runOnTop(); i < pile.size(); ++i) {
    view.run.push_back(WireCard(pile[i]));
  }
  view.finished = state.getFinished();
  if (const auto& play = state.getLastPlay(); play.has_value()) {
    moonbase::games::CastleLastPlay last;
    last.playerId = play->playerId;
    for (const cards::Card& card : play->cards) last.cards.push_back(WireCard(card));
    last.burned = play->burned;
    last.pickedUp = play->pickedUp;
    view.lastPlay = std::move(last);
  }
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const castle::Player& seat = state.getPlayer(static_cast<int>(i));
    moonbase::games::CastlePlayer player;
    player.playerId = seat.getId();
    player.ready = seat.isReady();
    player.handCount = static_cast<int>(seat.getHand().size());
    // The viewer's own seat, on turn: whether a play exists, or else
    // the pile is theirs to pick up (a blind row flips instead). Nobody
    // else's hand is the viewer's to read, and an over game seats nobody
    // on turn.
    player.canPlay = seat.getId() == viewer_id && state.getWhoseTurn() == static_cast<int>(i) &&
                     state.hasLegalPlay(static_cast<int>(i));
    // Own hand faces only, everyone's once the game ends; face-down
    // rows are a count for everyone, their holder included.
    if (ended || seat.getId() == viewer_id) {
      for (const cards::Card& card : seat.getHand()) player.hand.push_back(WireCard(card));
    }
    for (const cards::Card& card : seat.getFaceUp()) player.faceUp.push_back(WireCard(card));
    player.faceDownCount = static_cast<int>(seat.getFaceDown().size());
    player.out = seat.isOut();
    view.players.push_back(std::move(player));
  }
  return view;
}

GameEvents GolfHub::JoinedEventLocked(const std::string& game_id, const GameEntry& entry,
                                      const std::string& viewer_id) const {
  if (entry.kind == GameKind::kCastle) {
    moonbase::games::CastleGameJoined joined;
    joined.view = CastleViewLocked(game_id, entry, viewer_id);
    return CastleUpdateEvent(CastleUpdate::FromGamejoined(std::move(joined)));
  }
  moonbase::games::GameJoined joined;
  joined.view = ViewLocked(game_id, entry, viewer_id);
  return GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined)));
}

void GolfHub::StageGameViewsLocked(const std::string& game_id, const GameEntry& entry,
                                   Outbox& outbox) const {
  for (const std::string& recipient : entry.roster) {
    if (entry.kind == GameKind::kCastle) {
      moonbase::games::CastleGameState update;
      update.view = CastleViewLocked(game_id, entry, recipient);
      outbox.To(recipient, CastleUpdateEvent(CastleUpdate::FromGamestate(std::move(update))));
      continue;
    }
    moonbase::games::GameStateUpdate update;
    update.view = ViewLocked(game_id, entry, recipient);
    outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
  }
}

void GolfHub::StageGameOverLocked(Room& room, const std::string& game_id, Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.started()) return;
  if (game->second.kind == GameKind::kCastle) {
    // Final views (every hand face up), then the finish order; the
    // engine names a loser only for a game over by play, and an
    // abandonment has no finish order at all — nobody went out.
    const castle::GameState& state = game->second.castle();
    moonbase::games::CastleGameEnded ended;
    ended.finished = state.getFinished();
    ended.loser = state.loser();
    StageGameViewsLocked(game_id, game->second, outbox);
    for (const std::string& recipient : game->second.roster) {
      outbox.To(recipient, CastleUpdateEvent(CastleUpdate::FromGameended(ended)));
      player_game_.erase(recipient);
    }
    room.games.erase(game);
    return;
  }
  const golf::GameState& state = game->second.golf();

  // Seat order, so the display string is stable. Winners come from the
  // roster's seats; the scores keep every seat, abandoned or not.
  const auto winner_indexes = WinnersAmong(state, game->second.roster);
  std::vector<std::string> winner_ids;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    if (winner_indexes.contains(static_cast<int>(i))) {
      winner_ids.push_back(state.getPlayer(static_cast<int>(i)).getName().value_or(""));
    }
  }

  moonbase::games::GameEnded ended;
  ended.winner = absl::StrJoin(winner_ids, " & ");
  ended.winners = winner_ids;
  for (const golf::Player& seat : state.getPlayers()) {
    moonbase::games::FinalScore score;
    score.playerId = seat.getName().value_or("");
    score.score = seat.score();
    ended.finalScores.push_back(std::move(score));
  }

  // Final views (everything face up), then the result; the game itself
  // is done and gone locally.
  StageGameViewsLocked(game_id, game->second, outbox);
  for (const std::string& recipient : game->second.roster) {
    outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromGameended(ended)));
    player_game_.erase(recipient);
  }
  room.games.erase(game);
}

void GolfHub::FinalizeGameLocked(const std::string& room_id, Room& room, const std::string& game_id,
                                 Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.started()) return;

  // Room-scoped running stats: every roster seat played, every winner
  // won. With a store these same deltas already rode the finish commit;
  // this mirrors them into the local rows (and IS the update in-memory).
  for (const HubStore::StatsDelta& delta :
       StatsDeltasOf(*game->second.state, game->second.roster)) {
    const auto member = room.members.find(delta.player_id);
    if (member == room.members.end()) continue;
    member->second.games_played += delta.played;
    member->second.games_won += delta.won;
    member->second.total_score += delta.score;
  }

  StageGameOverLocked(room, game_id, outbox);
  // The terminal row is the durable handoff to other instances. It
  // remains until the room is deleted and its foreign-key cascade runs;
  // deleting it here can outrun a listener that has not handled the
  // finish wake yet.
  StageRoomStateLocked(room_id, outbox);
}

}  // namespace games_hub
