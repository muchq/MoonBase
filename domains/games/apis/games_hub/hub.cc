#include "domains/games/apis/games_hub/hub.h"

#include <algorithm>
#include <deque>
#include <set>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "domains/games/apis/games_hub/adapters.h"
#include "domains/games/apis/games_hub/chess_archive.h"
#include "domains/games/apis/games_hub/game_events.h"
#include "domains/games/apis/games_hub/memory_hub_store.h"
#include "domains/games/apis/games_hub/pong_bots.h"
#include "domains/games/apis/games_hub/protocol_input.h"
#include "domains/games/apis/games_hub/round_robin.h"
#include "domains/games/apis/games_hub/round_robin_wire.h"
#include "domains/games/apis/games_hub/splat.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/pong/rally.h"
#include "domains/games/libs/pong/table.h"

namespace games_hub {

using moonbase::games::ChessUpdate;
using moonbase::games::GameCommands;
using moonbase::games::GameEvents;
using moonbase::games::PongUpdate;

namespace {

std::size_t MaxSeatsOf(GameKind kind) {
  return ForKind(kind, [](auto adapter) { return decltype(adapter)::kMaxSeats; });
}

// The one place a stored row becomes a wire message. Live delivery and
// history replay share it, so the two can never describe the same
// message differently.
moonbase::games::ChatMessage ChatEvent(const ChatRow& row) {
  moonbase::games::ChatMessage message;
  message.messageId = row.message_id;
  message.playerId = row.player_id;
  message.text = row.text;
  message.sentAtUnixMillis = row.sent_at_unix_millis;
  if (IsBotAuthor(row.player_id)) message.bot = true;
  if (row.player_id == kWordchainPlayerId) message.wordchain = WordchainOfText(row.text);
  return message;
}

// The machinery that is one game's alone, typed on the engine of the game
// whose adapter opts into it (adapters.h's EngineWith).
using ClockedTable = EngineWith<kClocked>;
using Clock = Adapter<ClockedTable>;
using BotTable = EngineWith<kEngineBots>;
using RallyTable = EngineWith<kRealtime>;
using Feed = Adapter<EngineWith<kPublished>>;
using RallyGame = Adapter<RallyTable>;

template <typename... F>
struct Overloaded : F... {
  using F::operator()...;
};

// Bounded rebase-retry for the conditional commits: each miss adopts the
// stored truth, so giving up leaves a consistent hub and a client who
// can simply resend.
constexpr int kMaxCommitAttempts = 3;

// A challenge's terms as the lobby lists them: "Standard starting
// position · 3+2", a clock of whole minutes in minutes and any other in
// seconds.
std::string TermsLine(const ChessTerms& terms) {
  const int64_t initial_s = terms.time_control.initial_ms / 1000;
  const std::string initial =
      initial_s % 60 == 0 ? absl::StrCat(initial_s / 60) : absl::StrCat(initial_s, "s");
  return absl::StrCat(chess_play::ChessSetupName(terms.setup_id).value_or(terms.setup_id), " · ",
                      initial, "+", terms.time_control.increment_ms / 1000);
}

// The games a chess, pong or spades table has finished; zero for anything
// else, and before a table starts.
std::size_t GamesOf(const std::optional<HostedState>& state) {
  if (!state.has_value()) return 0;
  return VisitAdapter(*state, [](auto adapter, const auto& engine) {
    return decltype(adapter)::GamesFinished(engine);
  });
}

// The table's phase for the room's lobby summary, whichever game it plays.
std::string PhaseStringOf(const HostedState& state) {
  return VisitAdapter(
      state, [](auto adapter, const auto& engine) { return decltype(adapter)::Phase(engine); });
}

// The occupant whose turn it is, or "" between turns and at the end.
std::string CurrentTurnOf(const HostedState& state) {
  return VisitAdapter(
      state, [](auto adapter, const auto& engine) { return decltype(adapter)::Turn(engine); });
}

std::string InstanceId() {
  absl::BitGen gen;
  return absl::StrCat("hub-", absl::Hex(absl::Uniform<uint64_t>(gen), absl::kZeroPad16));
}

// The per-seat stat deltas a finish that closes the table applies — the
// payload of CommitGameFinish, and the same numbers FinalizeGameLocked
// mirrors into the local member rows.
StatsDeltas StatsDeltasOf(const HostedState& state, const std::vector<std::string>& roster) {
  return VisitAdapter(state, [&](auto adapter, const auto& engine) {
    return decltype(adapter)::ClosedDeltas(engine, roster);
  });
}

// What a transition credits when it ends a game without closing the
// table; a finish that closes it is StatsDeltasOf's.
std::optional<StatsDeltas> DealEndDeltas(const HostedState& before, const HostedState& after) {
  return VisitPair(
      before, after,
      [](auto adapter, const auto& was, const auto& now) {
        return decltype(adapter)::DealEndDeltas(was, now);
      },
      std::optional<StatsDeltas>{});
}

// Whether a transition began a table's next game: the moment a game
// starts.
bool DealStarted(const HostedState& before, const HostedState& after) {
  return VisitPair(
      before, after,
      [](auto adapter, const auto& was, const auto& now) {
        return decltype(adapter)::DealStarted(was, now);
      },
      false);
}

// The seat a leaver vacates, in whichever engine: compacted while seats
// remain, or the game resolved (golf keeps every seat for the scorecard;
// castle, rummy and chess abandon below two seats).
std::optional<HostedState> WithoutSeat(const HostedState& state, const std::string& player_id,
                                       int64_t now_ms) {
  return VisitAdapter(state, [&](auto adapter, const auto& engine) -> std::optional<HostedState> {
    const int seat = engine.playerIndex(player_id);
    if (seat < 0) return HostedState(engine);
    auto next = decltype(adapter)::WithoutSeat(engine, seat, now_ms);
    return HostedState(next.ok() ? *std::move(next) : engine);
  });
}

}  // namespace

GamesHub::GamesHub(std::shared_ptr<TicketVault> vault, std::shared_ptr<cards::Dealer> dealer,
                 std::shared_ptr<IdGenerator> ids, std::chrono::seconds grace_period,
                 std::shared_ptr<futility::otel::MetricsRecorder> metrics,
                 std::shared_ptr<HubStore> store, std::shared_ptr<ChatStore> chat_store,
                 RateLimits limits, HubTestHooks hooks)
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
      chess_opener_([](std::string_view setup_id) {
        absl::BitGen gen;
        return chess_play::SelectChessSetup(setup_id, gen);
      }),
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
// assemble a series out of two nested loops first. Each game's envelope is
// the exception: its series are its union's cases, listed in its adapter
// (GameCounterSeries) beside the union they copy.
//
// hub_e2e_test pins one of the two directions. ExpectOnlyDeclaredCounterSeries
// runs in every stream test's TearDown — and in every SecondInstance's
// destructor — so a series emitted but not declared fails, including from the
// tests that refuse admissions, trip the rate limiter or drop a stream, which
// is where most of these fire. The other direction, a value declared here that
// nothing ever emits, has no such guard: it costs a permanently flat line on a
// dashboard, and the only thing in its way is the literal copy of this list in
// BuildingAHandlerDeclaresEveryCounterSeriesAtZero, which has to be edited too
// — except for the stream command/event blocks, whose values are the model's
// union cases and are pinned both ways by the *SeriesMatchTheModelUnions
// tests in hub_e2e_test.
const std::vector<GamesHub::CounterSeries>& GamesHub::DeclaredCounterSeries() {
  static const auto* kSeries = [] {
    // Each game's envelope first, from its adapter's lists.
    auto* series = new std::vector<CounterSeries>(GameCounterSeries());
    series->insert(series->end(), {
      {"chat_appends", {{"result", "stored"}}},
      {"chat_appends", {{"result", "rejected"}}},
      {"chat_appends", {{"result", "unavailable"}}},
      {"chat_catch_up_drains", {}},
      {"chat_failures", {{"stage", "cursor_seed"}}},
      {"chat_failures", {{"stage", "catch_up"}}},
      {"chat_failures", {{"stage", "history_load"}}},
      {"chat_history_replays", {}},
      {"chat_rows_delivered", {}},
      {"hub_admissions_refused", {{"reason", "bad_ticket"}}},
      {"hub_admissions_refused", {{"reason", "seat_conflict"}}},
      // The room layer's own GameCommands cases on hub_commands —
      // CountCommand's naming, spelled out (see the model-pin note above).
      {"hub_commands", {{"command", "createRoom"}}},
      {"hub_commands", {{"command", "joinRoom"}}},
      {"hub_commands", {{"command", "leaveRoom"}}},
      {"hub_commands", {{"command", "getRoomState"}}},
      {"hub_commands", {{"command", "chat"}}},
      {"hub_disconnects", {{"kind", "clean"}}},
      {"hub_disconnects", {{"kind", "abrupt"}}},
      // The room layer's own GameEvents cases on hub_events — Send's
      // naming.
      {"hub_events", {{"event", "sessionReady"}}},
      {"hub_events", {{"event", "roomState"}}},
      {"hub_events", {{"event", "roomLeft"}}},
      {"hub_events", {{"event", "roomChat"}}},
      {"hub_events", {{"event", "roomChatHistory"}}},
      {"hub_events", {{"event", "commandRejected"}}},
      // The lobby envelope on its own series, as each game's is: the
      // LobbyAction and LobbyUpdate cases, pinned by
      // LobbySeriesMatchTheModelUnions.
      {"lobby_commands", {{"command", "join"}}},
      {"lobby_commands", {{"command", "move"}}},
      {"lobby_commands", {{"command", "shape"}}},
      {"lobby_commands", {{"command", "leave"}}},
      {"lobby_commands", {{"command", "setGeometry"}}},
      {"lobby_events", {{"event", "worldState"}}},
      {"lobby_events", {{"event", "playerJoined"}}},
      {"lobby_events", {{"event", "playerMoved"}}},
      {"lobby_events", {{"event", "shapeChanged"}}},
      {"lobby_events", {{"event", "playerLeft"}}},
      {"lobby_events", {{"event", "geometryChanged"}}},
      {"lobby_events", {{"event", "tape"}}},
      // A room's voice, the same way: VoiceAction and VoiceUpdate,
      // pinned by VoiceSeriesMatchTheModelUnions.
      {"voice_commands", {{"command", "join"}}},
      {"voice_commands", {{"command", "leave"}}},
      {"voice_commands", {{"command", "signal"}}},
      {"voice_events", {{"event", "roster"}}},
      {"voice_events", {{"event", "joined"}}},
      {"voice_events", {{"event", "left"}}},
      {"voice_events", {{"event", "signal"}}},
      // deja's tape: what was asked for, what came back, and how
      // much of it reached a wall. lobby_tape_poller_active is the gauge
      // beside them, and gauges are not declared.
      {"lobby_tape_polls", {{"result", "ok"}}},
      {"lobby_tape_polls", {{"result", "failed"}}},
      {"lobby_tape_splats", {}},
      {"hub_rate_limited", {{"kind", "chat"}}},
      {"hub_rate_limited", {{"kind", "command"}}},
      {"hub_rate_limited", {{"kind", "lobby"}}},
      {"hub_rate_limited", {{"kind", "paddle"}}},
      {"hub_rate_limited", {{"kind", "voice"}}},
      // One entry per RejectKind, in enum order.
      {"hub_rejections", {{"kind", "rate_limited"}}},
      {"hub_rejections", {{"kind", "invalid"}}},
      {"hub_rejections", {{"kind", "state"}}},
      {"hub_rejections", {{"kind", "rules"}}},
      {"hub_rejections", {{"kind", "unavailable"}}},
      {"hub_rejections", {{"kind", "unknown"}}},
      {"hub_restored_seats_reaped", {}},
      {"hub_seats_expired", {}},
      {"hub_sessions", {{"resumed", "true"}}},
      {"hub_sessions", {{"resumed", "false"}}},
      {"bot_requests", {{"bot", "microgpt"}, {"result", "ok"}}},
      {"bot_requests", {{"bot", "microgpt"}, {"result", "empty"}}},
      {"bot_requests", {{"bot", "microgpt"}, {"result", "busy"}}},
      {"bot_requests", {{"bot", "microgpt"}, {"result", "rate_limited"}}},
      {"bot_requests", {{"bot", "microgpt"}, {"result", "unreachable"}}},
      {"bot_requests", {{"bot", "microgpt"}, {"result", "error"}}},
      {"bot_requests", {{"bot", "mithril"}, {"result", "ok"}}},
      {"bot_requests", {{"bot", "mithril"}, {"result", "empty"}}},
      {"bot_requests", {{"bot", "mithril"}, {"result", "busy"}}},
      {"bot_requests", {{"bot", "mithril"}, {"result", "rate_limited"}}},
      {"bot_requests", {{"bot", "mithril"}, {"result", "unreachable"}}},
      {"bot_requests", {{"bot", "mithril"}, {"result", "error"}}},
    });
    return series;
  }();
  return *kSeries;
}

void GamesHub::DeclareMetrics() {
  if (!metrics_) return;
  for (const CounterSeries& series : DeclaredCounterSeries()) {
    metrics_->DeclareCounter(series.name, series.attributes);
  }
}

GamesHub::~GamesHub() {
  bots_.clear();
  {
    const std::lock_guard<std::mutex> lock(reaper_mu_);
    reaper_stop_ = true;
  }
  reaper_cv_.notify_all();
  if (boot_reaper_.joinable()) boot_reaper_.join();
  {
    const std::lock_guard<std::mutex> lock(tape_mu_);
    tape_stop_ = true;
  }
  tape_cv_.notify_all();
  if (tape_poller_.joinable()) tape_poller_.join();
  {
    const std::lock_guard<std::mutex> lock(heartbeat_mu_);
    heartbeat_stop_ = true;
  }
  heartbeat_cv_.notify_all();
  if (heartbeat_.joinable()) heartbeat_.join();
  {
    const std::lock_guard<std::mutex> lock(chess_clock_mu_);
    chess_clock_stop_ = true;
  }
  chess_clock_cv_.notify_all();
  if (chess_clocks_.joinable()) chess_clocks_.join();
  {
    const std::lock_guard<std::mutex> lock(chess_bot_mu_);
    chess_bot_stop_ = true;
  }
  chess_bot_cv_.notify_all();
  if (chess_bots_.joinable()) chess_bots_.join();
  {
    const std::lock_guard<std::mutex> lock(pong_tick_mu_);
    pong_tick_stop_ = true;
  }
  pong_tick_cv_.notify_all();
  if (pong_ticks_.joinable()) pong_ticks_.join();
}

absl::Status GamesHub::RestoreFromStore() {
  // Once, before serving. A second restore would stack a new cohort
  // behind a reaper that never re-arms, leaving those members in
  // forever, so refuse it loudly.
  if (restored_) {
    return absl::FailedPreconditionError("RestoreFromStore already ran");
  }
  auto snapshot = store_->LoadSnapshot();
  if (!snapshot.ok()) return snapshot.status();
  const std::lock_guard<std::mutex> lock(mu_);
  for (const HubStore::RoomRow& row : snapshot->rooms) {
    rooms_[row.room_id].chess_published = row.chess_published;
    world_.SetSurface(row.room_id, row.surface);
  }
  for (const HubStore::ChessEventRow& row : snapshot->events) {
    rooms_[row.room_id].round_robins.insert_or_assign(row.event_id, row);
  }
  for (const HubStore::MemberRow& row : snapshot->members) {
    // Presence seeds from the row, the fleet truth ReconcileRoomLocked
    // adopts on every wake; seeding false would make the first
    // channel-active after a boot see phantom movement and inject a
    // roomState into a resuming seat's hydration.
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
    entry.terms = row.terms;
    entry.event = row.event;
    entry.games_announced = GamesOf(entry.state);
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

void GamesHub::BootReaperMain() {
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
    if (ReapUnlessResumedElsewhere(player_id)) Count("hub_restored_seats_reaped");
  }
}

namespace {

moonbase::games::TapeGuess TopGuess(const moonbase::deja::Guess& guess) {
  moonbase::games::TapeGuess top;
  top.token = guess.token;
  top.p = guess.p;
  return top;
}

// deja's event as the wall shows it: the chips, both predictors' best
// guess, what actually happened, and the verdict — plus the spot, which
// the hub derives rather than carries, so two clients in a room draw the
// same event in the same place without a word about it on the wire.
moonbase::games::TapeSplat SplatOf(const moonbase::deja::DejaEvent& event) {
  const Splat spot = SplatFor(event.seq);
  moonbase::games::TapeSplat splat;
  splat.seq = event.seq;
  splat.ts = event.ts;
  splat.wall = spot.wall;
  splat.u = spot.u;
  splat.v = spot.v;
  splat.context = event.context;
  splat.actual = event.actual;
  splat.verdict = event.verdict;
  if (!event.predictions.bigram.empty()) splat.bigram = TopGuess(event.predictions.bigram.front());
  if (!event.predictions.net.empty()) splat.net = TopGuess(event.predictions.net.front());
  return splat;
}

}  // namespace

void GamesHub::AttachTape(std::shared_ptr<deja::Client> tape) { tape_ = std::move(tape); }

void GamesHub::StartRoomBot(std::shared_ptr<microgpt::Client> client, BotLimits limits) {
  StartBot(MicrogptResponder(std::move(client), chat_store_), limits);
}

void GamesHub::StartWordchain(std::shared_ptr<mithril::Client> client, BotLimits limits) {
  StartBot(WordchainResponder(std::move(client)), limits);
}

void GamesHub::StartBot(std::shared_ptr<Responder> responder, BotLimits limits) {
  bots_.push_back(std::make_unique<RoomBot>(
      std::move(responder), chat_store_, [this](const std::string& room_id) { PumpChat(room_id); },
      metrics_, instance_id_, limits));
}

void GamesHub::StartRoomHeartbeat(std::chrono::milliseconds interval) {
  if (heartbeat_.joinable()) return;
  heartbeat_ = std::thread([this, interval] {
    while (true) {
      StampHeldRooms();
      SweepStaleRooms();
      std::unique_lock<std::mutex> lock(heartbeat_mu_);
      heartbeat_cv_.wait_for(lock, interval, [this] { return heartbeat_stop_; });
      if (heartbeat_stop_) return;
    }
  });
}

void GamesHub::SweepStaleRooms() {
  store_->Enqueue(
      {HubStore::SweepRooms{std::chrono::duration_cast<std::chrono::seconds>(kRoomStaleAfter)},
       HubStore::SweepPublishedChess{kPublishedChessKept}});
}

void GamesHub::StampHeldRooms() {
  // Registry ids are the seats this instance owns: live sockets and ones
  // parked inside its grace. Rooms seen only through rows are left out.
  // Snapshotted before mu_, so the registry's lock never nests inside it.
  const std::vector<std::string> seats = registry_.Ids();
  std::set<std::string> held;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    for (const std::string& player_id : seats) {
      if (const auto room = player_room_.find(player_id); room != player_room_.end()) {
        held.insert(room->second);
      }
    }
  }
  if (held.empty()) return;
  store_->Enqueue({HubStore::TouchRooms{{held.begin(), held.end()}}});
}

void GamesHub::StartTapePolling(std::shared_ptr<deja::Client> tape) {
  if (tape_poller_.joinable()) return;
  AttachTape(std::move(tape));
  if (!tape_) return;
  tape_poller_ = std::thread([this] { TapePollerMain(); });
}

void GamesHub::TapePollerMain() {
  absl::BitGen gen;
  while (true) {
    PollTapeOnce();
    std::unique_lock<std::mutex> lock(tape_mu_);
    const auto tick =
        std::chrono::milliseconds(absl::Uniform(gen, kTapePollMin.count(), kTapePollMax.count()));
    tape_cv_.wait_for(lock, tick, [this] { return tape_stop_; });
    if (tape_stop_) return;
  }
}

bool GamesHub::PollTapeOnce() {
  if (!tape_) return false;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const bool occupied = world_.AnyoneInAGlasshouse();
    if (occupied != tape_polling_) {
      tape_polling_ = occupied;
      // Delta form, like hub_sessions_active: 1 while this instance is
      // asking deja for tape, 0 while no glass is occupied.
      if (metrics_) metrics_->RecordGauge("lobby_tape_poller_active", occupied ? 1 : -1);
      // An idle stretch is not a backlog owed to the next occupant: the
      // poll that resumes takes the newest seq and shows nothing.
      if (!occupied) tape_priming_ = true;
    }
    if (!occupied) return false;
  }
  // Off mu_. deja can be slow, dead, or wrong, and none of that may touch
  // the hub's lock or stall a socket.
  auto recent = tape_->Recent(tape_seq_);
  Count("lobby_tape_polls", {{"result", recent.ok() ? "ok" : "failed"}});
  if (!recent.ok()) return true;

  // Only a poll that got an answer consumes the priming flag. A failed
  // first poll that cleared it would leave the next one dumping deja's
  // whole ring onto glass somebody just walked up to.
  const bool priming = tape_priming_;
  tape_priming_ = false;
  // A poll that comes back from an outage can carry deja's whole ring,
  // and no wall wants more than it holds. Everything past the newest
  // kTapeMemory is advanced over rather than drawn.
  std::size_t fresh = 0;
  for (const auto& event : recent->events) {
    if (event.seq > tape_seq_) ++fresh;
  }
  const std::size_t skipped = fresh > World::kTapeMemory ? fresh - World::kTapeMemory : 0;
  std::size_t seen = 0;
  int splats = 0;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    World::Deliveries deliveries;
    for (const auto& event : recent->events) {
      // deja answers "after N" from a ring it may have re-served; seq is
      // the identity, and it only ever moves forward.
      if (event.seq <= tape_seq_) continue;
      tape_seq_ = event.seq;
      if (priming) continue;
      if (seen++ < skipped) continue;
      world_.Splat(SplatOf(event), deliveries);
      ++splats;
    }
    SendWorldLocked(deliveries);
  }
  if (splats > 0 && metrics_) metrics_->RecordCounter("lobby_tape_splats", splats);
  return true;
}

void GamesHub::SeedChatCursorLocked(const std::string& room_id) {
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

void GamesHub::EnqueueWritesLocked(Writes& writes) {
  if (writes.empty()) return;
  for (const HubStore::Op& op : writes) {
    std::visit(
        [this](const auto& write) {
          using Write = std::decay_t<decltype(write)>;
          if constexpr (std::is_same_v<Write, HubStore::Notify> ||
                        std::is_same_v<Write, HubStore::TouchRooms> ||
                        std::is_same_v<Write, HubStore::SweepRooms> ||
                        std::is_same_v<Write, HubStore::SweepPublishedChess>) {
            // A wake changes no row; a stamp changes none a client sees;
            // a room sweep's deletes reach clients through its own wakes,
            // and the feed's are no room's.
          } else if constexpr (std::is_same_v<Write, HubStore::UpsertMember>) {
            TouchRoomLocked(write.row.room_id);
          } else {
            TouchRoomLocked(write.room_id);
          }
        },
        op);
  }
  store_->Enqueue(std::move(writes));
  writes.clear();
}

void GamesHub::TouchRoomLocked(const std::string& room_id) {
  const auto room = rooms_.find(room_id);
  if (room != rooms_.end()) room->second.revision = ++room_revisions_;
}

void GamesHub::StageLocked(Writes& writes, HubStore::Op op) const {
  writes.push_back(std::move(op));
}

void GamesHub::StageMemberLocked(const std::string& room_id, const std::string& player_id,
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

void GamesHub::StageWakeLocked(const std::string& room_id, Writes& writes) const {
  StageLocked(writes, HubStore::Notify{RoomChannel(room_id), instance_id_});
}

GamesHub::Commit GamesHub::CommitEntryLocked(const std::string& room_id, const std::string& game_id,
                                           GameEntry& entry, const std::vector<std::string>& roster,
                                           const std::optional<HostedState>& state,
                                           const std::vector<HubStore::StatsDelta>* finish,
                                           const std::optional<ChessTerms>* terms) {
  const int64_t version = entry.version + 1;
  const std::optional<ChessTerms> next_terms =
      state.has_value() ? std::nullopt : (terms != nullptr ? *terms : entry.terms);
  // Whether the table was already over before this transition — a commit
  // loop that rebased onto another instance's finish holds its ended
  // state, and the retry then commits over the terminal row. That game
  // was played once, and was recorded by whoever ended it.
  const bool was_over = entry.started() && IsOver(*entry.state);
  const bool game_was_over = GameOverWith<kPublished>(entry.kind, entry.state);
  const bool table_game_was_over = TableGameOver(entry);
  // Who played: a leave that ends the game commits without its leaver.
  const std::vector<std::string> seated = entry.roster;
  // Stamped before the outcome is known: a commit whose fate is unknown
  // may still have landed.
  TouchRoomLocked(room_id);
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
  row.terms = next_terms;
  row.event = entry.event;
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
    entry.terms = (*stored)->terms;
    entry.event = (*stored)->event;
    // Another instance committed the table: its rally is that one's now.
    pong_rallies_.erase({room_id, game_id});
    if (HasCapability(entry.kind, kRealtime)) pong_elsewhere_.insert({room_id, game_id});
    return Commit::kRebased;
  }
  entry.roster = roster;
  entry.terms = next_terms;
  if (state.has_value()) entry.state.emplace(*state);
  entry.version = version;
  // The instance that commits a pong table runs its rally (from a game's
  // end on, TickPongOnce lets it go).
  pong_elsewhere_.erase({room_id, game_id});
  HostPongLocked(room_id, game_id, entry);
  // The game-finished event, here and nowhere downstream: this
  // is the one place that knows the finish landed and that this instance
  // is what ended it. StageGameOverLocked runs the same ceremony on
  // every instance holding the room, off the durable terminal row, and
  // an unavailable store leaves a live row somebody finishes again —
  // either would count the game twice.
  if (finish != nullptr && !was_over) {
    if (const auto finished = FinishedOf(*state, roster.size()); finished.has_value()) {
      RecordLocked([&](absl::Time now) { return GameFinishedLine(now, room_id, *finished); });
    }
  }
  // A tagged table's game just ended: its result is a row the archive
  // now holds, which this instance reads back for its round robins.
  if (entry.event.has_value() && !table_game_was_over && TableGameOver(entry)) {
    RefreshRoundRobinGamesLocked(room_id);
  }
  // A chess game this instance just ended, in a published room, is in
  // the public feed now: 1d4 is asked to index its players for the month.
  // The feed dates it by the database's clock, not this one, so in a
  // month's first minute the month before is asked for too. Bots have no
  // games there of their own.
  if (chess_indexer_ && !game_was_over && GameOverWith<kPublished>(entry.kind, entry.state)) {
    if (const auto room = rooms_.find(room_id);
        room != rooms_.end() && room->second.chess_published) {
      const absl::Time now = absl::FromUnixMillis(NowMs());
      std::vector<std::string> months = {absl::FormatTime("%Y-%m", now, absl::UTCTimeZone())};
      if (std::string before =
              absl::FormatTime("%Y-%m", now - absl::Minutes(1), absl::UTCTimeZone());
          before != months[0]) {
        months.push_back(std::move(before));
      }
      for (const std::string& player_id : seated) {
        if (IsBotId(player_id)) continue;
        for (const std::string& month : months) chess_indexer_({player_id, month});
      }
    }
  }
  return Commit::kCommitted;
}

void GamesHub::SetChessIndexer(ChessIndexer indexer) {
  const std::lock_guard<std::mutex> lock(mu_);
  chess_indexer_ = std::move(indexer);
}

void GamesHub::SetEventWriter(EventWriter writer) {
  const std::lock_guard<std::mutex> lock(mu_);
  event_writer_ = std::move(writer);
}

void GamesHub::AttachListener(pg::Listener* listener) {
  const std::lock_guard<std::mutex> lock(mu_);
  listener_ = listener;
  if (listener_ == nullptr) return;
  for (const auto& [room_id, room] : rooms_) {
    listener_->Listen(RoomChannel(room_id));
    listener_->Listen(ChatChannel(room_id));
  }
}

void GamesHub::OnNotify(const std::string& channel, const std::string& payload) {
  WakeChannel(channel, payload, /*from_active=*/false);
}

void GamesHub::OnChannelActive(const std::string& channel) {
  WakeChannel(channel, /*payload=*/"", /*from_active=*/true);
}

void GamesHub::WakeChannel(const std::string& channel, const std::string& payload,
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

void GamesHub::CatchUpRoom(const std::string& room_id, bool project_always) {
  Outbox outbox;
  for (int attempt = 0;; ++attempt) {
    uint64_t revision_before_read = 0;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      // Only rooms we hold: a stale wake for a dropped room must not
      // resurrect it (join is the one path that materializes rooms).
      const auto room = rooms_.find(room_id);
      if (room == rooms_.end()) return;
      revision_before_read = room->second.revision;
    }
    // Off mu_: reconnect fires this once per room on the poll thread, and a
    // sibling's every commit fires it too; holding the lock across
    // Flush+LoadRoom would stall every move/chat/join for the whole burst.
    // Same shape as PumpChat.
    store_->Flush();
    auto rows = store_->LoadRoom(room_id);
    if (!rows.ok()) {
      LOG(WARNING) << "room " << room_id << " catch-up failed: " << rows.status();
      return;
    }
    const std::lock_guard<std::mutex> lock(mu_);
    const auto room = rooms_.find(room_id);
    if (room == rooms_.end()) return;
    if (room->second.revision == revision_before_read) {
      ReconcileRoomLocked(room_id, *rows, outbox, project_always);
      break;
    }
    // The room moved locally during the read, so the rows may show it as
    // it was before: a game since ended still live, one since created
    // absent, a member since dropped still seated. Reconciling that would
    // roll the room back. Read again; a room that keeps moving gets its
    // read under the lock, where nothing local moves.
    if (attempt + 1 < kCatchUpReadAttempts) continue;
    RefreshRoomLocked(room_id, outbox, project_always);
    break;
  }
  Deliver(outbox);
}

bool GamesHub::RefreshRoomLocked(const std::string& room_id, Outbox& outbox, bool project_always) {
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
  ReconcileRoomLocked(room_id, *rows, outbox, project_always);
  return true;
}

bool GamesHub::ReconcileRoomLocked(const std::string& room_id, const HubStore::RoomRows& rows,
                                  Outbox& outbox, bool project_always) {
  // The rows are trusted as a complete snapshot of the room; that they
  // are current is the caller's promise, kept by reading under mu_ or by
  // CatchUpRoom's revision check.
  if (!rows.exists) {
    // Deleted by another instance; nothing local can outrank that.
    const auto room = rooms_.find(room_id);
    if (room == rooms_.end()) return false;
    for (const auto& [member_id, member] : room->second.members) {
      if (auto it = player_room_.find(member_id);
          it != player_room_.end() && it->second == room_id) {
        player_room_.erase(it);
        player_game_.erase(member_id);
        LeaveWorldLocked(member_id);  // its world goes with it
        LeaveVoiceLocked(member_id);  // and its voice
      }
    }
    rooms_.erase(room);
    world_.ForgetSurface(room_id);
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
  // The row's surface is the room's, every time: a sibling's setGeometry
  // reaches this instance as the row, and a room conjured from a member
  // row in a torn boot snapshot (the room row landing between the
  // snapshot's reads) has no surface until a read like this one hands
  // it over. Standing players are placed on the new surface either way.
  const bool reshaped = rows.surface != world_.SurfaceOf(room_id);
  if (reshaped) {
    World::Deliveries deliveries;
    world_.Reshape(room_id, rows.surface, deliveries);
    SendWorldLocked(deliveries);
  }
  ListenRoomLocked(room_id);  // idempotent; covers a room just materialized
  // A cursor born in the same critical section that makes the room held:
  // no member exists locally yet, so no append can commit behind it and
  // then be stepped over — the adoption race a wake-time seed would have.
  if (materialized) SeedChatCursorLocked(room_id);

  // A sibling's publish reaches this instance as the row; the members
  // held here hear it without a `by`, which the row does not carry. It is
  // a change like any other, so a catch-up that read before it rereads.
  const bool republished = rows.chess_published != room.chess_published;
  room.chess_published = rows.chess_published;
  bool changed = materialized || reshaped || republished;

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
      StopWatchingLocked(member_id);  // while its room is still known
      player_room_.erase(it);
      player_game_.erase(member_id);
      LeaveWorldLocked(member_id);  // a sibling's drop takes the world too
      LeaveVoiceLocked(member_id);  // and the room's voice
    }
  }
  room.members = std::move(members);
  if (republished && !materialized) StagePublishedLocked(room, rows.chess_published, {}, outbox);

  // A sibling's round robin, or its change to one, reaches this instance
  // as the row; so does a table it opened or closed for one, as the
  // games. The members held here hear each round robin that moved, after
  // its games' own news.
  std::map<std::string, std::map<int, std::string>> live_before;
  for (const auto& [id, held] : room.round_robins) live_before[id] = LiveTablesLocked(room, id);
  std::set<std::string> moved_round_robins;
  for (const HubStore::ChessEventRow& row : rows.events) {
    const auto held = room.round_robins.find(row.event_id);
    if (held != room.round_robins.end() && held->second.version >= row.version &&
        held->second.games == row.games) {
      continue;
    }
    changed = true;
    room.round_robins.insert_or_assign(row.event_id, row);
    moved_round_robins.insert(row.event_id);
  }

  // Games: adopt rows that moved past us. A row that ended while we
  // hold the game is a remote finish — ceremony here, then it is gone
  // locally; an ended row for a game we no longer hold was already
  // ceremonied and stays until the room's cascade.
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
      entry.terms = row.terms;
      entry.event = row.event;
      entry.games_announced = GamesOf(entry.state);
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
    // A table another instance committed: its rally is that one's.
    if (HasCapability(row.kind, kRealtime)) {
      pong_rallies_.erase({room_id, row.game_id});
      pong_elsewhere_.insert({room_id, row.game_id});
    }
    // A code re-minted for a table of the other game: the kind travels
    // with the state, or the next view would read the wrong engine, and
    // the chess table its watchers watched is gone.
    if (entry.kind != row.kind) DropWatchersLocked(row.game_id, entry, outbox);
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
    entry.terms = row.terms;
    entry.event = row.event;
    for (const std::string& member_id : entry.roster) player_game_[member_id] = row.game_id;
    if (over) StageGameOverLocked(room, row.game_id, outbox);
  }
  for (auto game = room.games.begin(); game != room.games.end();) {
    if (stored_ids.contains(game->first)) {
      ++game;
      continue;
    }
    changed = true;
    // Deleted remotely: a pre-start table whose last player left.
    DropWatchersLocked(game->first, game->second, outbox);
    for (const std::string& member_id : game->second.roster) {
      if (auto it = player_game_.find(member_id);
          it != player_game_.end() && it->second == game->first) {
        player_game_.erase(it);
      }
    }
    game = room.games.erase(game);
  }

  // An adoption moves the room as surely as a write does.
  if (changed) TouchRoomLocked(room_id);
  // Notify wake contract: always re-project. Active catch-up: only when
  // something moved, so a no-op reconnect does not fill session queues.
  if (changed || project_always) {
    for (const auto& [game_id, entry] : room.games) StageGameViewsLocked(game_id, entry, outbox);
    StageRoomStateLocked(room_id, outbox);
  }
  if (!materialized) {
    for (const auto& [id, row] : room.round_robins) {
      const auto before = live_before.find(id);
      if (moved_round_robins.contains(id) || before == live_before.end() ||
          before->second != LiveTablesLocked(room, id)) {
        StageRoundRobinLocked(room, row, outbox);
      }
    }
  }
  return changed;
}

void GamesHub::DropWatchersLocked(const std::string& game_id, GameEntry& entry,
                                 Outbox& outbox) {
  for (const std::string& watcher : entry.watchers) {
    moonbase::games::GameLeft left;
    left.gameId = game_id;
    outbox.To(watcher, LeftEvent(entry.kind, std::move(left)));
  }
  entry.watchers.clear();
}

void GamesHub::DropGameLocked(const GameRef& ref, Outbox& outbox) {
  DropWatchersLocked(ref.game_id, *ref.entry, outbox);
  for (const std::string& member_id : ref.entry->roster) {
    if (auto it = player_game_.find(member_id);
        it != player_game_.end() && it->second == ref.game_id) {
      player_game_.erase(it);
    }
  }
  ref.room->games.erase(ref.game_id);
}

void GamesHub::ListenRoomLocked(const std::string& room_id) {
  if (listener_ == nullptr) return;
  listener_->Listen(RoomChannel(room_id));
  listener_->Listen(ChatChannel(room_id));
}

void GamesHub::UnlistenRoomLocked(const std::string& room_id) {
  if (listener_ == nullptr) return;
  listener_->Unlisten(RoomChannel(room_id));
  listener_->Unlisten(ChatChannel(room_id));
}

// What a wire a resume takes over is closed with: 4001 tells a browser it
// was superseded (CloseEvent.code), not dropped (model/games.smithy, Play).
static const opal::http::CloseReason kSuperseded{.code = 4001, .reason = "superseded"};
// The kicked session unwinds within the grace, so admission after the kick
// waits longer than that before refusing.
static constexpr std::chrono::milliseconds kKickGrace{250};
static constexpr std::chrono::milliseconds kAdmissionAfterKick{1000};
static_assert(kKickGrace < kAdmissionAfterKick);

opal::eventstream::StreamTask GamesHub::Play(moonbase::games::PlayInput input,
                                            moonbase::games::PlayAsyncServerStream& stream) {
  if (HasEmbeddedNul(input.ticket)) {
    Count("hub_admissions_refused", {{"reason", "bad_ticket"}});
    co_return opal::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  auto player = vault_->SpendTicket(input.ticket);
  if (!player.has_value()) {
    Count("hub_admissions_refused", {{"reason", "bad_ticket"}});
    co_return opal::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  const std::string player_id = *player;

  // The blessed admission call (ADR-0022): pre-first-suspend, on the
  // launching handler thread, where its brief blocking is legal.
  const auto mint = [&stream] { return stream.Share(); };
  auto admission = registry_.ResumeOrAdd(player_id, mint, std::chrono::milliseconds(0));
  if (admission == Registry::Admission::kRefused) {
    // The ticket was minted from this player's resume token, so this dial
    // is the player: the live wire it collides with is one they left, such
    // as a phone's socket whose close never arrived. Kicking it parks the
    // seat, and this dial resumes it. The kick waits for no answer from a
    // peer that may be gone, and tells one that is still there (a
    // duplicated tab) it was superseded, so it does not redial.
    registry_.Kick(player_id, kSuperseded, kKickGrace);
    admission = registry_.ResumeOrAdd(player_id, mint, kAdmissionAfterKick);
  }
  if (admission == Registry::Admission::kRefused) {
    Count("hub_admissions_refused", {{"reason", "seat_conflict"}});
    co_return opal::Error::Modeled("SeatConflict", "player already has a live connection");
  }
  Count("hub_sessions",
        {{"resumed", admission == Registry::Admission::kResumed ? "true" : "false"}});
  TrackActive(+1);

  // Membership decides the resync, not the registry: a player restored
  // from the store admits as new — their old process's
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

  // Per-session budgets, owned by this coroutine frame: frames
  // are handled sequentially per session, so no locking, and the state
  // dies with the connection. Every frame draws from its envelope's
  // bucket (lobby, voice, or command); chat draws from its own tighter
  // bucket too, because each message is a durable database transaction
  // plus fleet-wide fan-out.
  TokenBucket command_budget(limits_.command_burst, limits_.command_refill_per_sec);
  TokenBucket lobby_budget(limits_.lobby_burst, limits_.lobby_refill_per_sec);
  TokenBucket chat_budget(limits_.chat_burst, limits_.chat_refill_per_sec);
  TokenBucket voice_budget(limits_.voice_burst, limits_.voice_refill_per_sec);
  TokenBucket paddle_budget(limits_.paddle_burst, limits_.paddle_refill_per_sec);

  while (true) {
    auto received = co_await stream.Receive();
    if (!received.ok() && received.error().kind() == opal::ErrorKind::kValidation) {
      // An event over the model's bounds (opal ADR-0025): refused in band,
      // session kept, charged to the command bucket.
      if (command_budget.Admit(std::chrono::steady_clock::now())) {
        Reject(player_id, RejectKind::kInvalid, received.error().message());
      } else {
        Count("hub_rate_limited", {{"kind", "command"}});
        Reject(player_id, RejectKind::kRateLimited, "slow down");
      }
      continue;
    }
    if (!received.ok() || !received->has_value()) {
      // Any close parks the seat for the grace window (ADR-0020) —
      // expiry reaps it, a resume reclaims it. A clean close carries no
      // leave intent: a closed tab sends the same close frame (#1236),
      // and the deliberate exit is the explicit leaveRoom command.
      // Detach fails only when the entry is already gone; the session
      // counters are its alone.
      // The world is presence, not membership: the player leaves it now
      // and rejoins on resume, while the seat parks. Before Detach, so a
      // resume admitted the instant the seat is released finds no stale
      // entry to refuse its join.
      {
        const std::lock_guard<std::mutex> lock(mu_);
        LeaveWorldLocked(player_id);
        // Voice too: the peer connections died with the socket, and a
        // resume joins again.
        LeaveVoiceLocked(player_id);
        // And a watch, which a resume asks for again.
        StopWatchingLocked(player_id);
      }
      // Away before the seat parks too: a resume can take the seat back the
      // instant it does, and must be the room's last word on the player.
      SetConnected(player_id, false);
      if (auto current = CurrentRoom(player_id)) BroadcastRoom(*current);
      if (hooks_.before_seat_release) hooks_.before_seat_release(player_id);
      const bool released = registry_.Detach(player_id);
      if (hooks_.after_seat_release) hooks_.after_seat_release(player_id);
      if (released) {
        TrackActive(-1);
        Count("hub_disconnects", {{"kind", received.ok() ? "clean" : "abrupt"}});
      }
      co_return opal::Unit{};
    }
    const auto now = std::chrono::steady_clock::now();
    const bool lobby = (*received)->as_lobby_or_null() != nullptr;
    const bool voice = (*received)->as_voice_or_null() != nullptr;
    const bool paddle = OnRealtimeBudget(**received);
    TokenBucket& budget = lobby    ? lobby_budget
                          : voice  ? voice_budget
                          : paddle ? paddle_budget
                                   : command_budget;
    if (!budget.Admit(now)) {
      // Refused before any locked work: the whole point is that a flood
      // costs the hub almost nothing. The session stays open — the
      // buckets already bound the damage, and closing floods just
      // converts them into reconnect load.
      Count("hub_rate_limited", {{"kind", lobby    ? "lobby"
                                          : voice  ? "voice"
                                          : paddle ? "paddle"
                                                   : "command"}});
      Reject(player_id, RejectKind::kRateLimited, "slow down");
      continue;
    }
    if ((*received)->as_chat_or_null() != nullptr && !chat_budget.Admit(now)) {
      Count("hub_rate_limited", {{"kind", "chat"}});
      Reject(player_id, RejectKind::kRateLimited, "slow down");
      continue;
    }
    HandleCommand(player_id, **received);
  }
}

void GamesHub::HandleCommand(const std::string& player_id, const GameCommands& command) {
  CountCommand(command);
  if (const auto* create = command.as_createRoom_or_null()) {
    // The room's surface, chosen once here; absent is the plane.
    Surface surface;
    if (create->geometry.has_value()) {
      auto chosen = SurfaceFromGeometry(*create->geometry);
      if (!chosen.ok()) {
        Reject(player_id, RejectKind::kInvalid, std::string(chosen.status().message()));
        return;
      }
      surface = *chosen;
    }
    std::string room_id;
    Outbox outbox;
    Writes writes;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!player_room_.contains(player_id)) {
        room_id = ids_->RoomId();
        while (rooms_.contains(room_id)) room_id = ids_->RoomId();
        world_.SetSurface(room_id, surface);
        const auto [member, inserted] = rooms_[room_id].members.emplace(player_id, Member{});
        // Born at zero with its room: it provably has no rows, and the
        // creator's first message must pump from the very beginning.
        chat_cursors_.emplace(room_id, ChatCursor{});
        LeaveWorldLocked(player_id);  // out of the plaza's world
        player_room_[player_id] = room_id;
        ListenRoomLocked(room_id);
        StageLocked(writes, HubStore::UpsertRoom{room_id, surface});
        StageMemberLocked(room_id, player_id, member->second, writes);
        StageRoomStateLocked(room_id, outbox);
        EnqueueWritesLocked(writes);
        RecordLocked([&room_id, surface = SurfaceKindName(surface)](absl::Time now) {
          return RoomCreatedLine(now, room_id, surface);
        });
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
        // The room may live on another instance: one
        // synchronous read materializes it here before refusing.
        if (!rooms_.contains(join->roomId)) {
          store_answered = RefreshRoomLocked(join->roomId, outbox, /*project_always=*/true);
        }
        const auto room = rooms_.find(join->roomId);
        if (room != rooms_.end()) {
          const auto [member, inserted] = room->second.members.emplace(player_id, Member{});
          LeaveWorldLocked(player_id);  // out of the plaza's world
          player_room_[player_id] = join->roomId;
          StageMemberLocked(join->roomId, player_id, member->second, writes);
          StageWakeLocked(join->roomId, writes);
          StageRoomStateLocked(join->roomId, outbox);
          EnqueueWritesLocked(writes);
          joined = true;
          RecordLocked([&id = join->roomId, members = room->second.members.size()](absl::Time now) {
            return RoomJoinedLine(now, id, members);
          });
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
        LeaveEverywhere(player_id, outbox, writes);
        // Last: what leaving the table sends the leaver comes first, and
        // nothing about the room may follow the ack that it is gone.
        outbox.To(player_id, GameEvents::FromRoomleft(std::move(ack)));
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
    // through WithMember, and mu_ is not recursive. The store's guard is
    // the membership check, and it holds the lock across the append so
    // the answer cannot go stale.
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

    absl::StatusOr<ChatRow> appended =
        chat_store_->Append(room_id, player_id, chat->text, instance_id_);
    if (appended.status().code() == absl::StatusCode::kFailedPrecondition &&
        WithMember(room_id, player_id, [] {})) {
      // The store's check reads rows this instance may not have written
      // yet: a room or seat taken a moment ago is still in its queue.
      // Once it lands, the store answers for the seat this hub holds.
      store_->Flush();
      appended = chat_store_->Append(room_id, player_id, chat->text, instance_id_);
    }
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
    {
      // That a room was talking, and how many were in it. The
      // text never leaves the process. This path dropped mu_ before the
      // append — the store re-takes it — so it is taken back here, for
      // the room's size and for the writer it guards.
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room = rooms_.find(room_id);
      const std::size_t members = room == rooms_.end() ? 0 : room->second.members.size();
      RecordLocked(
          [&room_id, members](absl::Time now) { return ChatMessageLine(now, room_id, members); });
    }

    // The committed row reaches locals through the pump, like every
    // other row. That is not indirection for its own sake: a remote
    // append our commit raced past holds a lower id, and the pump's
    // cursor walk delivers it before ours — staging just our own row
    // and advancing the cursor over it would skip it for good.
    PumpChat(room_id);
    // After the asker's own row is out, so a reply can only follow it.
    for (const auto& bot : bots_) bot->OnMessage(*appended);
    return;
  }

  const bool played = AnyAdapter([&](auto adapter) {
    using A = decltype(adapter);
    const auto* move = A::MoveOf(command);
    if (move == nullptr) return false;
    HandleGameMove<typename A::Engine>(player_id, *move);
    return true;
  });
  if (played) return;

  if (const auto* lobby = command.as_lobby_or_null()) {
    HandleLobby(player_id, lobby->action);
    return;
  }

  if (const auto* voice = command.as_voice_or_null()) {
    HandleVoice(player_id, voice->action);
    return;
  }

  Reject(player_id, RejectKind::kUnknown, "unknown command");
}

void GamesHub::HandleLobby(const std::string& player_id,
                          const moonbase::games::LobbyAction& action) {
  std::optional<World::Refusal> refusal;
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
      } else {
        move_coalescing_.Forget(player_id);
      }
    } else if (const auto* set = action.as_setGeometry_or_null()) {
      auto surface = SurfaceFromGeometry(set->geometry);
      if (!surface.ok()) {
        refusal = World::Refusal{RejectKind::kInvalid, std::string(surface.status().message())};
      } else {
        // The room's for everyone: the row carries it to other instances,
        // whose reconcile reshapes their standing players; the plaza has
        // no row, so its shape is this instance's until it restarts.
        const std::string world = WorldOfLocked(player_id);
        world_.Reshape(world, *surface, deliveries);
        RecordLocked([&world, kind = SurfaceKindName(*surface)](absl::Time now) {
          return GeometryChangedLine(now, world, kind);
        });
        if (rooms_.contains(world)) {
          Writes writes;
          StageLocked(writes, HubStore::SetRoomSurface{world, *surface});
          StageWakeLocked(world, writes);
          EnqueueWritesLocked(writes);
        }
      }
    } else {
      refusal = World::Refusal{RejectKind::kUnknown, "unknown command"};
    }
    SendWorldLocked(deliveries);
    if (hooks_.after_world_sent) hooks_.after_world_sent();
  }
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

std::string GamesHub::WorldOfLocked(const std::string& player_id) const {
  const auto room = player_room_.find(player_id);
  return room != player_room_.end() ? room->second : std::string(World::kPlaza);
}

// Under mu_, straight to the registry (which only queues): the world's
// deliveries leave in the order the world changed, ahead of anything the
// next command under this lock can send.
void GamesHub::SendWorldLocked(World::Deliveries& deliveries) {
  for (auto& delivery : deliveries) {
    opal::server::DeliveryClass delivery_class = move_coalescing_.For(delivery.to, delivery.update);
    moonbase::games::LobbyEvent event;
    event.update = std::move(delivery.update);
    Send(delivery.to, GameEvents::FromLobby(std::move(event)), std::move(delivery_class));
  }
  deliveries.clear();
}

void GamesHub::LeaveWorldLocked(const std::string& player_id) {
  World::Deliveries deliveries;
  world_.Leave(player_id, deliveries);
  move_coalescing_.Forget(player_id);
  SendWorldLocked(deliveries);
}

void GamesHub::HandleVoice(const std::string& player_id,
                          const moonbase::games::VoiceAction& action) {
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    Voice::Deliveries deliveries;
    if (action.as_join_or_null() != nullptr) {
      const auto room = player_room_.find(player_id);
      if (room == player_room_.end()) {
        refusal = Refusal{RejectKind::kState, "join a room first"};
      } else {
        refusal = voice_.Join(player_id, room->second, deliveries);
      }
    } else if (action.as_leave_or_null() != nullptr) {
      if (!voice_.Leave(player_id, deliveries))
        refusal = Refusal{RejectKind::kState, "not in voice"};
    } else if (const auto* signal = action.as_signal_or_null()) {
      refusal = voice_.Signal(player_id, *signal, deliveries);
    } else {
      refusal = Refusal{RejectKind::kUnknown, "unknown voice action"};
    }
    SendVoiceLocked(deliveries);
  }
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

void GamesHub::SendVoiceLocked(Voice::Deliveries& deliveries) {
  for (auto& delivery : deliveries) {
    moonbase::games::VoiceEvent event;
    event.update = std::move(delivery.update);
    Send(delivery.to, GameEvents::FromVoice(std::move(event)));
  }
  deliveries.clear();
}

void GamesHub::LeaveVoiceLocked(const std::string& player_id) {
  Voice::Deliveries deliveries;
  voice_.Leave(player_id, deliveries);
  SendVoiceLocked(deliveries);
}

void GamesHub::SetIceServers(std::vector<moonbase::games::IceServer> servers) {
  const std::lock_guard<std::mutex> lock(mu_);
  voice_.SetIceServers(std::move(servers));
}

template <typename E>
void GamesHub::HandleGameMove(const std::string& player_id,
                              const typename Adapter<E>::Move& move) {
  using A = Adapter<E>;
  constexpr GameKind kind = A::kKind;
  auto action = A::Decode(move, player_id);
  std::visit(
      Overloaded{
          [&](Refusal& refusal) { Reject(player_id, std::move(refusal)); },
          [&](CreateTable&) { CreateGameMove(player_id, kind); },
          [&](JoinTable& join) { JoinGameMove(player_id, join.game_id, kind); },
          [&](LeaveTable&) { LeaveTableMove(player_id, kind); },
          [&](StartGame<typename A::Start>& start) { StartMove<E>(player_id, std::move(start)); },
          [&](Transition<E>& transition) { TableEngineMove<E>(player_id, transition); },
          [&](WatchTable& watch) { WatchMove(player_id, watch.game_id, kind); },
          [&](AddBot& bot) { AddBotMove(player_id, bot, kind); },
          [&](PostTerms& post) { PostTermsMove(player_id, std::move(post.terms), kind); },
          [&](AskHistory&) { ChessHistoryMove(player_id); },
          [&](AskReview& ask) { ChessReviewMove(player_id, ask.review); },
          [&](Publish& publish) { PublishChessMove(player_id, publish.published); },
          [&](CreateRoundRobin& create) {
            CreateRoundRobinMove(player_id, create.entrants, kind, create.terms, create.variant);
          },
          [&](Withdraw& withdraw) { WithdrawMove(player_id, withdraw.withdraw, kind); },
          [&](Forfeit& forfeit) { ForfeitMove(player_id, forfeit.forfeit, kind); },
          [&](ListRoundRobins&) { RoundRobinsMove(player_id, kind); },
          [&](PlayRoundRobin& play) { PlayRoundRobinMove(player_id, play.play, kind); },
          [&](Steer& steer) { PaddleMove(player_id, steer.paddle); },
      },
      action);
}

void GamesHub::LeaveTableMove(const std::string& player_id, GameKind kind) {
  Outbox outbox;
  Writes writes;
  bool in_game = false;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    in_game = player_game_.contains(player_id);
    if (in_game) {
      LeaveGameLocked(player_id, outbox, writes);
    } else if (HasCapability(kind, kWatchable)) {
      if (const auto watched = StopWatchingLocked(player_id)) {
        // A watcher gets up the way a seat does.
        in_game = true;
        moonbase::games::GameLeft ack;
        ack.gameId = *watched;
        outbox.To(player_id, LeftEvent(kind, std::move(ack)));
      }
    }
    EnqueueWritesLocked(writes);
  }
  if (in_game) {
    Deliver(outbox);
  } else {
    Reject(player_id, RejectKind::kState, "not in a game");
  }
}

std::function<void()> GamesHub::FilledStartLocked(const std::string& player_id,
                                                  const GameEntry& entry) {
  return ForKind(entry.kind, [&](auto adapter) -> std::function<void()> {
    using A = decltype(adapter);
    auto start = A::FilledStart(entry.terms, entry.event);
    if (!start.has_value()) return nullptr;
    return [this, player_id, start = *std::move(start)] {
      StartMove<typename A::Engine>(player_id, start);
    };
  });
}

void GamesHub::CreateGameMove(const std::string& player_id, GameKind kind) {
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
        StopWatchingLocked(player_id);
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

void GamesHub::JoinGameMove(const std::string& player_id, const std::string& game_id,
                           GameKind kind) {
  if (HasEmbeddedNul(game_id)) {
    Reject(player_id, RejectKind::kInvalid, "invalid game id");
    return;
  }
  Outbox outbox;
  std::optional<Refusal> refusal;
  std::function<void()> filled;
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
        store_answered = RefreshRoomLocked(room_id, outbox, /*project_always=*/true);
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
          if (entry.roster.size() >= MaxSeatsOf(entry.kind)) {
            refusal = Refusal{RejectKind::kState, "game is full"};
            break;
          }
          if (const Pairing* pairing = PairingOfLocked(*room, entry); pairing != nullptr) {
            const bool opened_by_white = std::find(entry.roster.begin(), entry.roster.end(),
                                                   pairing->white) != entry.roster.end();
            const std::string& opponent = opened_by_white ? pairing->black : pairing->white;
            if (player_id != opponent) {
              refusal = Refusal{RejectKind::kState, absl::StrCat("that table is for ", opponent)};
              break;
            }
            if (Voided(*pairing, room->round_robins.at(entry.event->event_id).withdrawn)) {
              refusal = Refusal{RejectKind::kState, "that table's pairing is void"};
              break;
            }
          }
          std::vector<std::string> roster = entry.roster;
          roster.push_back(player_id);
          const Commit commit =
              CommitEntryLocked(room_id, game_id, entry, roster, std::nullopt, nullptr);
          if (commit == Commit::kRebased) continue;
          if (commit == Commit::kGone) {
            DropWatchersLocked(game_id, entry, outbox);
            room->games.erase(game_id);
            refusal = Refusal{RejectKind::kState, "game not found"};
            break;
          }
          if (commit == Commit::kUnavailable) {
            refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
            break;
          }
          StopWatchingLocked(player_id);
          player_game_[player_id] = game_id;
          if (entry.roster.size() >= MaxSeatsOf(entry.kind)) {
            filled = FilledStartLocked(player_id, entry);
          }

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
  // Whatever the refresh staged is true either way; a refused join is
  // not a reason to drop it.
  Deliver(outbox);
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
    return;
  }
  if (filled) filled();
}

template <typename E>
void GamesHub::StartMove(const std::string& player_id,
                         StartGame<typename Adapter<E>::Start> request) {
  using A = Adapter<E>;
  bool seated = false;
  if (request.next_game) {
    bool tagged = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto ref = FindGameLocked(player_id);
      seated = ref.has_value() && ref->entry->started();
      tagged = ref.has_value() && ref->entry->event.has_value();
      if constexpr ((A::kCapabilities & kRoundRobins) != 0) {
        if (tagged) {
          const auto held = ref->room->round_robins.find(ref->entry->event->event_id);
          A::RoundRobinStart(request.start, ref->entry->terms,
                             held == ref->room->round_robins.end() ? nullptr : &held->second);
        }
      }
    }
    if (seated && tagged) {
      Reject(player_id, RejectKind::kState, "a round robin's table plays one game");
      return;
    }
  }
  // The opening is drawn off the lock.
  if constexpr ((A::kCapabilities & kOpenings) != 0) {
    if (const absl::Status opened = A::Prepare(request.start, chess_opener_); !opened.ok()) {
      Reject(player_id, RejectKind::kRules, std::string(opened.message()));
      return;
    }
  }
  if constexpr ((A::kCapabilities & kNextGame) != 0) {
    if (seated) {
      TableEngineMove<E>(player_id,
                         Transition<E>{[start = std::move(request.start)](
                                           const E& engine, int, const MoveContext& context) {
                           return A::Next(engine, start, context);
                         }});
      return;
    }
  }
  StartGameMove<E>(player_id, std::move(request.start));
}

template <typename E>
void GamesHub::StartGameMove(const std::string& player_id, typename Adapter<E>::Start start) {
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
        const OpenContext context{ref->game_id, ref->entry->roster, NowMs(),
                                  PairingOfLocked(*ref->room, *ref->entry)};
        // The table opens on its own game, on `start` when the envelope
        // that asked is that game's and on the game's defaults otherwise.
        absl::StatusOr<HostedState> dealt =
            ForKind(ref->entry->kind, [&](auto adapter) -> absl::StatusOr<HostedState> {
              using T = decltype(adapter);
              typename T::Start table_start{};
              if constexpr (std::is_same_v<T, Adapter<E>>) table_start = start;
              auto opened = T::Open(context, table_start, std::move(deck));
              if (!opened.ok()) return opened.status();
              return HostedState(*std::move(opened));
            });
        if (!dealt.ok()) {
          refusal = Refusal{RejectKind::kRules, std::string(dealt.status().message())};
          break;
        }
        const Commit commit =
            CommitEntryLocked(ref->room_id, ref->game_id, *ref->entry, ref->entry->roster,
                              std::optional<HostedState>(*std::move(dealt)), nullptr);
        if (commit == Commit::kRebased) continue;  // the roster (or starter) raced us; redeal
        if (commit == Commit::kGone) {
          DropGameLocked(*ref, outbox);
          StageRoomStateLocked(ref->room_id, outbox);
          refusal = Refusal{RejectKind::kState, "game no longer exists"};
          break;
        }
        if (commit == Commit::kUnavailable) {
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        started = true;
        if (OpenStartsAGame(ref->entry->kind)) {
          RecordLocked([&id = ref->room_id, variant = GameKindName(ref->entry->kind),
                        seats = ref->entry->roster.size()](absl::Time now) {
            return GameStartedLine(now, id, variant, seats);
          });
        }
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

template <typename E>
void GamesHub::TableEngineMove(const std::string& player_id, const Transition<E>& move) {
  Outbox outbox;
  Writes writes;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      refusal = Refusal{RejectKind::kState, "not in a game"};
    } else {
      refusal = ApplyTableEngineMoveLocked<E>(*ref, player_id, move, outbox);
    }
    EnqueueWritesLocked(writes);
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
  } else {
    Deliver(outbox);
  }
}

template <typename E>
std::optional<Refusal> GamesHub::ApplyTableEngineMoveLocked(GameRef ref,
                                                           const std::string& player_id,
                                                           const Transition<E>& move,
                                                           Outbox& outbox) {
  constexpr GameKind kind = Adapter<E>::kKind;
  if (!ref.entry->started()) return Refusal{RejectKind::kState, "game not started"};
  // The move loop: pure transition off the entry, then the conditional
  // commit; a miss adopts the stored truth and replays the transition
  // against it. The kind is re-read each attempt: a rebase adopts
  // whatever the store holds under this code.
  for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
    if (ref.entry->kind != kind) {
      return Refusal{RejectKind::kState,
                     absl::StrCat("that table plays ", GameKindName(ref.entry->kind))};
    }
    const E& state = std::get<E>(*ref.entry->state);
    const int seat = state.playerIndex(player_id);
    if (seat < 0) return Refusal{RejectKind::kState, "not seated in this game"};
    const MoveContext context{
        NowMs(),
        [this] {
          std::deque<cards::Card> deck = dealer_->DealNewUnshuffledDeck();
          dealer_->ShuffleDeck(deck);
          return deck;
        },
        [room = ref.room](const std::string& id) {
          const auto member = room->members.find(id);
          return member == room->members.end() || !member->second.connected;
        }};
    auto next = move.next(state, seat, context);
    if (!next.ok()) {
      // A card the seat's row does not hold (absl::NotFound: castle's
      // RowIndexesOf, rummy's and spades' engines) is a client acting on
      // a view the table has moved past, not a rules refusal.
      const RejectKind reject_kind = next.status().code() == absl::StatusCode::kNotFound
                                         ? RejectKind::kState
                                         : RejectKind::kRules;
      return Refusal{reject_kind, std::string(next.status().message())};
    }
    // Occupant ids, not seats: the engine renumbers on a leave. The
    // opening turn (setup done) reads as a change from nobody.
    const std::string previous_turn = CurrentTurnOf(*ref.entry->state);
    const bool views_to_all = !move.views_to_all || move.views_to_all(state, *next);
    const bool over = next->isOver();
    HostedState next_state(*std::move(next));
    // A finish credits the room's stats in the same conditional commit
    // that lands it: the table's end, or a game's that leaves the table
    // playing on (a rummy deal).
    std::optional<std::vector<HubStore::StatsDelta>> deltas =
        DealEndDeltas(*ref.entry->state, next_state);
    const bool deal_ended = deltas.has_value();
    const bool deal_started = DealStarted(*ref.entry->state, next_state);
    if (over) deltas = StatsDeltasOf(next_state, ref.entry->roster);
    const Commit commit =
        CommitEntryLocked(ref.room_id, ref.game_id, *ref.entry, ref.entry->roster,
                          std::move(next_state), deltas.has_value() ? &*deltas : nullptr);
    if (commit == Commit::kRebased) continue;  // another instance moved first
    if (commit == Commit::kGone) {
      DropGameLocked(ref, outbox);
      StageRoomStateLocked(ref.room_id, outbox);
      return Refusal{RejectKind::kState, "game no longer exists"};
    }
    if (commit == Commit::kUnavailable) {
      return Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
    }
    if (deal_started) {
      RecordLocked([&id = ref.room_id, variant = VariantWordOf(*ref.entry->state),
                    seats = ref.entry->roster.size()](absl::Time now) {
        return GameStartedLine(now, id, variant, seats);
      });
    }
    if (move.first.has_value()) {
      for (const std::string& recipient : ref.entry->roster) outbox.To(recipient, *move.first);
    }
    if (over) {
      // Finalize stages the definitive final views itself.
      FinalizeGameLocked(ref.room_id, *ref.room, ref.game_id, outbox);
      return std::nullopt;
    }
    if (views_to_all) {
      StageGameViewsLocked(ref.game_id, *ref.entry, outbox);
    } else {
      outbox.To(player_id, StateEventOf(kind, ContextLocked(ref.game_id, *ref.entry), ref.entry->state,
                                        player_id));
    }
    if (deal_ended) {
      // The hand is the room's too: its stats moved.
      MirrorStatsLocked(*ref.room, *deltas);
    }
    // Either way the table's status in the room's listing moved.
    if (deal_ended || deal_started) StageRoomStateLocked(ref.room_id, outbox);
    // Between deals nobody is on turn, and that is no turn to announce.
    if (move.announce_turn) {
      const std::string current_turn = CurrentTurnOf(*ref.entry->state);
      if (current_turn != previous_turn && !current_turn.empty()) {
        moonbase::games::TurnChanged turn;
        turn.playerId = current_turn;
        for (const std::string& recipient : ref.entry->roster) {
          outbox.To(recipient, TurnEvent(kind, turn));
        }
      }
    }
    return std::nullopt;
  }
  return Refusal{RejectKind::kState, "game changed; try again"};
}

int64_t GamesHub::NowMs() const { return absl::ToUnixMillis(clock_ ? clock_() : absl::Now()); }

void GamesHub::SetChessOpener(ChessOpener opener) { chess_opener_ = std::move(opener); }

void GamesHub::SetClock(std::function<absl::Time()> clock) { clock_ = std::move(clock); }

int GamesHub::SweepChessClocksOnce() {
  struct Flagged {
    std::string room_id;
    std::string game_id;
  };
  std::vector<Flagged> flagged;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = NowMs();
    // A retry outlives nothing: a game gone from here, or over, whether it
    // ended on this instance or another, is no longer waiting on the store.
    std::erase_if(chess_flag_retry_at_, [this](const auto& retry) {
      const auto room = rooms_.find(retry.first.first);
      if (room == rooms_.end()) return true;
      const auto game = room->second.games.find(retry.first.second);
      if (game == room->second.games.end()) return true;
      const ClockedTable* table = CapableEngine<kClocked>(game->second.state);
      return table == nullptr || Clock::CurrentGameOver(*table);
    });
    for (const auto& [room_id, room] : rooms_) {
      for (const auto& [game_id, entry] : room.games) {
        const ClockedTable* table = CapableEngine<kClocked>(entry.state);
        if (table == nullptr || !Clock::FlagDue(*table, now)) continue;
        const auto retry = chess_flag_retry_at_.find({room_id, game_id});
        if (retry != chess_flag_retry_at_.end() && now < retry->second) continue;
        flagged.push_back({room_id, game_id});
      }
    }
  }
  // Each flag takes mu_ on its own: a commit is a store round trip, and a
  // slow store holds the hub for one game's, never for the sweep's.
  int ended = 0;
  for (const Flagged& flag : flagged) {
    Outbox outbox;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const std::pair<std::string, std::string> key{flag.room_id, flag.game_id};
      const auto room = rooms_.find(flag.room_id);
      if (room == rooms_.end()) {
        chess_flag_retry_at_.erase(key);
        continue;
      }
      const auto game = room->second.games.find(flag.game_id);
      const ClockedTable* table = game == room->second.games.end()
                                      ? nullptr
                                      : CapableEngine<kClocked>(game->second.state);
      if (table == nullptr || Clock::CurrentGameOver(*table)) {
        chess_flag_retry_at_.erase(key);
        continue;
      }
      const GameRef ref{flag.room_id, &room->second, flag.game_id, &game->second};
      const int64_t now = NowMs();
      // A rebase may find the game already ended elsewhere, or moved on:
      // the refusal says so, and the seat on turn sent nothing to refuse.
      const auto refusal = ApplyTableEngineMoveLocked<ClockedTable>(
          ref, Clock::Turn(*table),
          Transition<ClockedTable>{[now](const ClockedTable& clocked, int, const MoveContext&) {
            return Clock::Flag(clocked, now);
          }},
          outbox);
      if (refusal.has_value() && refusal->kind == RejectKind::kUnavailable) {
        // The store is down: try this game again later, not every tick.
        chess_flag_retry_at_[key] = now + kChessFlagRetry.count();
      } else {
        chess_flag_retry_at_.erase(key);
      }
      if (!refusal.has_value()) ++ended;
    }
    Deliver(outbox);
  }
  return ended;
}

void GamesHub::SetChessBotEngine(ChessBotEngine engine) { chess_bot_engine_ = std::move(engine); }

void GamesHub::AddBotMove(const std::string& player_id, const AddBot& bot, GameKind kind) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  std::function<void()> filled;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    bool seated = false;
    if (ref.has_value() && ref->entry->event.has_value()) {
      refusal = Refusal{RejectKind::kState, "a round robin's pairing is played by its pair"};
    } else if (bot.engine && chess_bot_engine_ == nullptr) {
      refusal = Refusal{RejectKind::kState, "no chess engine"};
    } else if (!ref.has_value()) {
      refusal = Refusal{RejectKind::kState, "not in a game"};
    } else {
      for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
        GameEntry& entry = *ref->entry;
        if (entry.kind != kind) {
          refusal = Refusal{RejectKind::kState,
                            absl::StrCat("that table plays ", GameKindName(entry.kind))};
          break;
        }
        if (entry.started()) {
          refusal = Refusal{RejectKind::kState, "game already started"};
          break;
        }
        if (entry.roster.size() >= MaxSeatsOf(entry.kind)) {
          refusal = Refusal{RejectKind::kState, "the table is full"};
          break;
        }
        std::vector<std::string> roster = entry.roster;
        roster.push_back(bot.bot_id);
        const Commit commit =
            CommitEntryLocked(ref->room_id, ref->game_id, entry, roster, std::nullopt, nullptr);
        if (commit == Commit::kRebased) continue;
        if (commit == Commit::kGone) {
          DropGameLocked(*ref, outbox);
          StageRoomStateLocked(ref->room_id, outbox);
          refusal = Refusal{RejectKind::kState, "game no longer exists"};
          break;
        }
        if (commit == Commit::kUnavailable) {
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        seated = true;
        filled = FilledStartLocked(player_id, entry);
        StageGameViewsLocked(ref->game_id, entry, outbox);
        StageRoomStateLocked(ref->room_id, outbox);
        break;
      }
      if (!refusal.has_value() && !seated) {
        refusal = Refusal{RejectKind::kState, "game changed; try again"};
      }
    }
  }
  if (refusal.has_value()) {
    Reject(player_id, std::move(*refusal));
    return;
  }
  Deliver(outbox);
  if (filled) filled();
}

void GamesHub::PostTermsMove(const std::string& player_id, ChessTerms terms, GameKind kind) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    bool posted = false;
    if (!ref.has_value()) {
      refusal = Refusal{RejectKind::kState, "not in a game"};
    } else {
      const std::optional<ChessTerms> next = std::move(terms);
      for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
        GameEntry& entry = *ref->entry;
        if (entry.kind != kind) {
          refusal = Refusal{RejectKind::kState,
                            absl::StrCat("that table plays ", GameKindName(entry.kind))};
          break;
        }
        if (entry.event.has_value()) {
          refusal = Refusal{RejectKind::kState, "a round robin's table plays on its terms"};
          break;
        }
        if (entry.started()) {
          refusal = Refusal{RejectKind::kState, "game already started"};
          break;
        }
        if (entry.roster.size() >= MaxSeatsOf(entry.kind)) {
          refusal = Refusal{RejectKind::kState, "the table is full"};
          break;
        }
        const Commit commit = CommitEntryLocked(ref->room_id, ref->game_id, entry, entry.roster,
                                                std::nullopt, nullptr, &next);
        if (commit == Commit::kRebased) continue;
        if (commit == Commit::kGone) {
          DropGameLocked(*ref, outbox);
          StageRoomStateLocked(ref->room_id, outbox);
          refusal = Refusal{RejectKind::kState, "game no longer exists"};
          break;
        }
        if (commit == Commit::kUnavailable) {
          refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
          break;
        }
        posted = true;
        StageGameViewsLocked(ref->game_id, entry, outbox);
        StageRoomStateLocked(ref->room_id, outbox);
        break;
      }
      if (!refusal.has_value() && !posted) {
        refusal = Refusal{RejectKind::kState, "game changed; try again"};
      }
    }
  }
  // A vanished table's news is true either way.
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

void GamesHub::WatchMove(const std::string& player_id, const std::string& game_id, GameKind kind) {
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
      bool store_answered = true;
      if (!room->games.contains(game_id)) {
        // Another instance may have opened it since our last wake.
        store_answered = RefreshRoomLocked(player_room_.at(player_id), outbox,
                                           /*project_always=*/true);
        room = FindRoomLocked(player_id);  // the refresh can drop us or the room
      }
      GameEntry* entry = nullptr;
      if (room != nullptr) {
        const auto game = room->games.find(game_id);
        if (game != room->games.end()) entry = &game->second;
      }
      if (entry == nullptr) {
        refusal = store_answered
                      ? Refusal{RejectKind::kState, "game not found"}
                      : Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
      } else if (entry->kind != kind) {
        refusal = Refusal{RejectKind::kState,
                          absl::StrCat("that table plays ", GameKindName(entry->kind))};
      } else {
        StopWatchingLocked(player_id);
        entry->watchers.insert(player_id);
        outbox.To(player_id,
                  StateEventOf(entry->kind, ContextLocked(game_id, *entry), entry->state, player_id));
      }
    }
  }
  // Whatever the refresh staged is true either way.
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

void GamesHub::ChessHistoryMove(const std::string& player_id) {
  const auto room_id = CurrentRoom(player_id);
  if (!room_id.has_value()) {
    Reject(player_id, RejectKind::kState, "not in a room");
    return;
  }
  // Staged publishes land first, so the flag read is the one last set.
  store_->Flush();
  auto history = store_->LoadChessHistory(*room_id, kChessHistoryLimit);
  if (!history.ok()) {
    Reject(player_id, RejectKind::kUnavailable, "storage unavailable; try again");
    return;
  }
  moonbase::games::ChessHistory answer;
  answer.published = history->published;
  for (const HubStore::ChessGameRow& row : history->games) {
    answer.games.push_back(ChessSummaryOf(row));
  }
  Send(player_id, Feed::Wrap(ChessUpdate::FromHistory(std::move(answer))));
}

void GamesHub::ChessReviewMove(const std::string& player_id,
                              const moonbase::games::ChessReviewRequest& review) {
  const bool by_table = review.gameId.has_value() && review.ordinal.has_value();
  if (review.archiveId.has_value() == by_table ||
      review.gameId.has_value() != review.ordinal.has_value()) {
    Reject(player_id, RejectKind::kInvalid, "name a game by archiveId, or by gameId and ordinal");
    return;
  }
  const auto room_id = CurrentRoom(player_id);
  if (!room_id.has_value()) {
    Reject(player_id, RejectKind::kState, "not in a room");
    return;
  }
  auto row = store_->LoadChessGame(
      *room_id, {review.archiveId, review.gameId.value_or(""), review.ordinal.value_or(0)});
  if (!row.ok()) {
    Reject(player_id, RejectKind::kUnavailable, "storage unavailable; try again");
    return;
  }
  if (!row->has_value()) {
    Reject(player_id, RejectKind::kState, "no such game in this room");
    return;
  }
  Send(player_id, Feed::Wrap(ChessUpdate::FromReview(ChessReviewOf(**row))));
}

void GamesHub::PublishChessMove(const std::string& player_id, bool published) {
  Outbox outbox;
  bool in_room = false;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const auto room_id = player_room_.find(player_id);
    Room* room = FindRoomLocked(player_id);
    in_room = room_id != player_room_.end() && room != nullptr;
    if (in_room) {
      room->chess_published = published;
      Writes writes;
      StageLocked(writes, HubStore::SetChessPublished{room_id->second, published});
      StageWakeLocked(room_id->second, writes);
      EnqueueWritesLocked(writes);
      StagePublishedLocked(*room, published, player_id, outbox);
    }
  }
  if (!in_room) {
    Reject(player_id, RejectKind::kState, "not in a room");
    return;
  }
  // The row before the word: every instance's archive reads the flag off
  // the row, so a game that ends after anyone hears this is archived on
  // it. One that ends between the click and the write was not announced.
  store_->Flush();
  Deliver(outbox);
}

void GamesHub::StagePublishedLocked(const Room& room, bool published,
                                   const std::optional<std::string>& by, Outbox& outbox) const {
  moonbase::games::ChessPublished update;
  update.published = published;
  update.by = by;
  for (const auto& [member_id, member] : room.members) {
    outbox.To(member_id, Feed::Wrap(ChessUpdate::FromPublished(update)));
  }
}

void GamesHub::CreateRoundRobinMove(const std::string& player_id,
                                   const std::vector<std::string>& entrants, GameKind kind,
                                   const ChessTerms& terms, pong::Variant variant) {
  auto pairings = RoundRobinPairings(entrants);
  if (!pairings.ok()) {
    Reject(player_id, RejectKind::kRules, std::string(pairings.status().message()));
    return;
  }
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    refusal = Refusal{RejectKind::kState, "round robin changed; try again"};
    for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
      // Looked up each attempt: a refresh can take the player or the room.
      const auto placed = player_room_.find(player_id);
      Room* room = placed == player_room_.end() ? nullptr : FindRoomLocked(player_id);
      if (room == nullptr) {
        refusal = Refusal{RejectKind::kState, "not in a room"};
        break;
      }
      const std::string room_id = placed->second;
      const auto stranger = std::find_if(
          entrants.begin(), entrants.end(),
          [&](const std::string& entrant) { return !room->members.contains(entrant); });
      if (stranger != entrants.end()) {
        refusal = Refusal{RejectKind::kRules, absl::StrCat(*stranger, " is not in the room")};
        break;
      }
      if (room->round_robins.size() >= kMaxRoundRobinsPerRoom) {
        refusal =
            Refusal{RejectKind::kState,
                    absl::StrCat("a room keeps at most ", kMaxRoundRobinsPerRoom, " round robins")};
        break;
      }
      HubStore::ChessEventRow row;
      row.room_id = room_id;
      row.event_id = ids_->GameCode();
      while (room->round_robins.contains(row.event_id)) row.event_id = ids_->GameCode();
      row.version = 1;
      row.creator = player_id;
      row.entrants = entrants;
      row.kind = kind;
      row.terms = terms;
      row.pong_variant = variant;
      row.pairings = *pairings;
      TouchRoomLocked(row.room_id);
      store_->Flush();  // the room row its insert needs
      const auto landed = store_->CommitChessEvent(row, instance_id_);
      if (!landed.ok()) {
        LOG(WARNING) << "round robin " << row.room_id << "/" << row.event_id
                     << " commit unavailable: " << landed.status();
        refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
        break;
      }
      if (*landed) {
        refusal.reset();
        StageRoundRobinLocked(*room, row, outbox);
        room->round_robins.insert_or_assign(row.event_id, std::move(row));
        break;
      }
      // A sibling took the id or the room: read what it did, and look again.
      if (!RefreshRoomLocked(room_id, outbox, /*project_always=*/false)) {
        refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
        break;
      }
    }
  }
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

void GamesHub::WithdrawMove(const std::string& player_id,
                           const moonbase::games::ChessWithdraw& withdraw, GameKind kind) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    refusal = ModerateRoundRobinLocked(
        player_id, withdraw.roundRobinId, kind, {withdraw.playerId},
        [&](HubStore::ChessEventRow& row) -> std::optional<Refusal> {
          const std::string& entrant = withdraw.playerId;
          if (std::find(row.entrants.begin(), row.entrants.end(), entrant) == row.entrants.end()) {
            return Refusal{RejectKind::kRules, absl::StrCat(entrant, " is not entered")};
          }
          if (row.withdrawn.contains(entrant)) {
            return Refusal{RejectKind::kState, absl::StrCat(entrant, " has already withdrawn")};
          }
          // A pairing in play is decided by its table, not voided.
          if (const auto seat = player_game_.find(entrant); seat != player_game_.end()) {
            const Room& room = *FindRoomLocked(player_id);
            if (const auto game = room.games.find(seat->second);
                game != room.games.end() && game->second.event.has_value() &&
                game->second.event->event_id == row.event_id && !TableGameOver(game->second)) {
              return Refusal{RejectKind::kState,
                             absl::StrCat(entrant, " is playing a pairing at ", seat->second)};
            }
          }
          row.withdrawn.insert(entrant);
          return std::nullopt;
        },
        outbox);
  }
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

void GamesHub::ForfeitMove(const std::string& player_id,
                          const moonbase::games::ChessForfeit& forfeit, GameKind kind) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    refusal = ModerateRoundRobinLocked(
        player_id, forfeit.roundRobinId, kind, {forfeit.winner, forfeit.loser},
        [&](HubStore::ChessEventRow& row) -> std::optional<Refusal> {
          const auto open =
              OpenPairing(EffectivePairings(row), row.withdrawn, forfeit.winner, forfeit.loser);
          if (!open.has_value()) {
            return Refusal{RejectKind::kState, absl::StrCat(forfeit.winner, " and ", forfeit.loser,
                                                            " have no pairing still to play")};
          }
          const auto live = LiveTablesLocked(*FindRoomLocked(player_id), row.event_id);
          if (const auto table = live.find(static_cast<int>(*open)); table != live.end()) {
            return Refusal{RejectKind::kState,
                           absl::StrCat("that pairing is being played at ", table->second)};
          }
          Pairing& pairing = row.pairings[*open];
          pairing.result =
              pairing.white == forfeit.winner ? PairingResult::kWhite : PairingResult::kBlack;
          pairing.forfeit = true;
          return std::nullopt;
        },
        outbox);
  }
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

std::optional<games_hub::Refusal> GamesHub::ModerateRoundRobinLocked(
    const std::string& player_id, const std::string& round_robin_id, GameKind kind,
    const std::vector<std::string>& parties,
    const std::function<std::optional<Refusal>(HubStore::ChessEventRow&)>& change, Outbox& outbox) {
  for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
    // Looked up each attempt: a refresh can take the player or the room.
    const auto placed = player_room_.find(player_id);
    Room* room = placed == player_room_.end() ? nullptr : FindRoomLocked(player_id);
    if (room == nullptr) return Refusal{RejectKind::kState, "not in a room"};
    const std::string room_id = placed->second;
    const auto held = room->round_robins.find(round_robin_id);
    if (held == room->round_robins.end()) {
      return Refusal{RejectKind::kState, "no such round robin in this room"};
    }
    const HubStore::ChessEventRow& row = held->second;
    if (row.kind != kind) {
      return Refusal{RejectKind::kState,
                     absl::StrCat("that round robin plays ", GameKindName(row.kind))};
    }
    // The creator moderates; while they are out of the room, any entrant
    // in it does, but not on what they are party to.
    if (room->members.contains(row.creator)) {
      if (row.creator != player_id) {
        return Refusal{RejectKind::kState, "only the round robin's creator can do that"};
      }
    } else if (std::find(row.entrants.begin(), row.entrants.end(), player_id) ==
               row.entrants.end()) {
      return Refusal{RejectKind::kState,
                     "only the round robin's entrants can do that while its creator is away"};
    } else if (std::find(parties.begin(), parties.end(), player_id) != parties.end()) {
      return Refusal{RejectKind::kState,
                     "while its creator is away, entrants can't moderate themselves"};
    }
    HubStore::ChessEventRow next = held->second;
    if (auto refused = change(next); refused.has_value()) return refused;
    next.version = held->second.version + 1;
    TouchRoomLocked(room_id);
    store_->Flush();
    const auto landed = store_->CommitChessEvent(next, instance_id_);
    if (!landed.ok()) {
      LOG(WARNING) << "round robin " << room_id << "/" << round_robin_id
                   << " commit unavailable: " << landed.status();
      return Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
    }
    if (*landed) {
      StageRoundRobinLocked(*room, next, outbox);
      held->second = std::move(next);
      return std::nullopt;
    }
    // A sibling moved it first: adopt its version, and change that.
    if (!RefreshRoomLocked(room_id, outbox, /*project_always=*/false)) {
      return Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
    }
  }
  return Refusal{RejectKind::kState, "round robin changed; try again"};
}

void GamesHub::RoundRobinsMove(const std::string& player_id, GameKind kind) {
  std::optional<GameEvents> answer;
  bool in_room = false;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    if (const Room* room = FindRoomLocked(player_id); room != nullptr) {
      in_room = true;
      std::vector<HeldRoundRobin> held;
      for (const auto& [id, row] : room->round_robins) {
        if (row.kind == kind) held.push_back({row, LiveTablesLocked(*room, id)});
      }
      answer = RoundRobinsEventOf(kind, held);
    }
  }
  if (!in_room) {
    Reject(player_id, RejectKind::kState, "not in a room");
    return;
  }
  if (answer.has_value()) Send(player_id, *std::move(answer));
}

void GamesHub::PlayRoundRobinMove(const std::string& player_id,
                                 const moonbase::games::ChessPlayRoundRobin& play, GameKind kind) {
  Outbox outbox;
  std::optional<Refusal> refusal;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    Room* room = FindRoomLocked(player_id);
    if (room == nullptr) {
      refusal = Refusal{RejectKind::kState, "not in a room"};
    } else if (player_game_.contains(player_id)) {
      refusal = Refusal{RejectKind::kState, "leave your current game first"};
    } else if (const auto held = room->round_robins.find(play.roundRobinId);
               held == room->round_robins.end()) {
      refusal = Refusal{RejectKind::kState, "no such round robin in this room"};
    } else if (held->second.kind != kind) {
      refusal = Refusal{RejectKind::kState,
                        absl::StrCat("that round robin plays ", GameKindName(held->second.kind))};
    } else {
      const HubStore::ChessEventRow& row = held->second;
      const auto open =
          OpenPairing(EffectivePairings(row), row.withdrawn, player_id, play.opponent);
      const auto live = LiveTablesLocked(*room, row.event_id);
      if (!open.has_value()) {
        refusal = Refusal{RejectKind::kState, absl::StrCat(player_id, " and ", play.opponent,
                                                           " have no pairing still to play")};
      } else if (const auto table = live.find(static_cast<int>(*open)); table != live.end()) {
        refusal = Refusal{RejectKind::kState,
                          absl::StrCat("that pairing is being played at ", table->second)};
      } else {
        const std::string room_id = player_room_.at(player_id);
        // A pong pairing starts on the round robin's variant, read when
        // its table fills; only a chess one waits on terms.
        const std::optional<ChessTerms> terms = RoundRobinTermsOf(kind, row);
        for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
          std::string game_id = ids_->GameCode();
          while (room->games.contains(game_id)) game_id = ids_->GameCode();
          GameEntry& entry = room->games[game_id];
          entry.kind = kind;
          entry.event = EventTag{row.event_id, static_cast<int>(*open)};
          const Commit commit = CommitEntryLocked(room_id, game_id, entry, {player_id},
                                                  std::nullopt, nullptr, &terms);
          if (commit == Commit::kRebased) continue;  // the code was taken; roll another
          if (commit != Commit::kCommitted) {
            room->games.erase(game_id);
            if (commit == Commit::kGone) continue;
            refusal = Refusal{RejectKind::kUnavailable, "storage unavailable; try again"};
            break;
          }
          StopWatchingLocked(player_id);
          player_game_[player_id] = game_id;
          moonbase::games::GameCreated announcement;
          announcement.gameId = game_id;
          announcement.createdBy = player_id;
          for (const auto& member : room->members) {
            outbox.To(member.first, CreatedEvent(kind, announcement));
          }
          outbox.To(player_id, JoinedEventLocked(game_id, entry, player_id));
          StageRoomStateLocked(room_id, outbox);
          StageRoundRobinLocked(*room, row, outbox);
          break;
        }
        if (!refusal.has_value() && !player_game_.contains(player_id)) {
          refusal = Refusal{RejectKind::kUnavailable, "could not allocate a game code; try again"};
        }
      }
    }
  }
  Deliver(outbox);
  if (refusal.has_value()) Reject(player_id, std::move(*refusal));
}

bool GamesHub::TableGameOver(const GameEntry& entry) {
  return GameOverWith<kRoundRobins>(entry.kind, entry.state);
}

std::map<int, std::string> GamesHub::LiveTablesLocked(const Room& room,
                                                     const std::string& round_robin_id) const {
  std::map<int, std::string> live;
  for (const auto& [game_id, entry] : room.games) {
    if (!entry.event.has_value() || entry.event->event_id != round_robin_id) continue;
    if (TableGameOver(entry)) continue;  // its game is the pairing's now
    live.emplace(entry.event->pairing, game_id);
  }
  return live;
}

const Pairing* GamesHub::PairingOfLocked(const Room& room, const GameEntry& entry) const {
  if (!entry.event.has_value()) return nullptr;
  const auto held = room.round_robins.find(entry.event->event_id);
  if (held == room.round_robins.end()) return nullptr;
  const auto& pairings = held->second.pairings;
  if (entry.event->pairing < 0 || entry.event->pairing >= std::ssize(pairings)) return nullptr;
  return &pairings[entry.event->pairing];
}

void GamesHub::RefreshRoundRobinGamesLocked(const std::string& room_id) {
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return;
  auto rows = store_->LoadRoom(room_id);
  if (!rows.ok()) {
    // The next wake's reconcile reads them instead.
    LOG(WARNING) << "room " << room_id << " round robin read failed: " << rows.status();
    return;
  }
  for (const HubStore::ChessEventRow& row : rows->events) {
    const auto held = room->second.round_robins.find(row.event_id);
    if (held == room->second.round_robins.end() || held->second.games == row.games) continue;
    held->second.games = row.games;
    round_robins_moved_.emplace_back(room_id, row.event_id);
  }
}

void GamesHub::StageRoundRobinLocked(const Room& room, const HubStore::ChessEventRow& row,
                                    Outbox& outbox) const {
  // In its own game's envelope.
  const std::optional<GameEvents> event =
      RoundRobinEventOf(row.kind, {row, LiveTablesLocked(room, row.event_id)});
  if (!event.has_value()) return;
  for (const auto& [member_id, member] : room.members) outbox.To(member_id, *event);
}

absl::StatusOr<std::string> GamesHub::ExportChessPgn(int64_t after_archive_id) {
  auto games = store_->LoadPublishedChess(after_archive_id, kChessHistoryLimit);
  if (!games.ok()) return games.status();
  std::string pgn;
  for (const HubStore::PublishedChessGame& game : *games) {
    if (!pgn.empty()) pgn += "\n";
    pgn += ChessPgnOf(game.archive_id, game.game, game.ended_at_ms);
  }
  return pgn;
}

absl::StatusOr<std::optional<moonbase::games::ChessReview>> GamesHub::PublishedChessReview(
    int64_t archive_id) {
  if (archive_id <= 0) return std::nullopt;  // ids count from 1
  // The feed's first game past the one before: this one, if the feed has it.
  auto games = store_->LoadPublishedChess(archive_id - 1, 1);
  if (!games.ok()) return games.status();
  if (games->empty() || games->front().archive_id != archive_id) return std::nullopt;
  return ChessReviewOf(games->front());
}

std::optional<std::string> GamesHub::StopWatchingLocked(const std::string& player_id) {
  Room* room = FindRoomLocked(player_id);
  if (room == nullptr) return std::nullopt;
  for (auto& [game_id, entry] : room->games) {
    if (entry.watchers.erase(player_id) > 0) return game_id;
  }
  return std::nullopt;
}

int GamesHub::PlayChessBotsOnce() {
  if (chess_bot_engine_ == nullptr) return 0;
  struct Turn {
    std::string room_id;
    std::string game_id;
    std::string bot_id;
    // Which of the table's games: its count of finished ones. A table
    // keeps its id, and can repeat a start, from one game to the next.
    std::size_t game_no;
    ChessBotAsk ask;
  };
  std::vector<Turn> turns;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = NowMs();
    // A retry holds back the game it was for, and only until it is due.
    std::erase_if(chess_bot_retry_at_, [this, now](const auto& retry) {
      if (retry.second.until <= now) return true;
      const auto room = rooms_.find(retry.first.first);
      if (room == rooms_.end()) return true;
      const auto game = room->second.games.find(retry.first.second);
      if (game == room->second.games.end()) return true;
      const BotTable* table = CapableEngine<kEngineBots>(game->second.state);
      return table == nullptr || table->scoreSheet().size() != retry.second.game;
    });
    for (const auto& [room_id, room] : rooms_) {
      for (const auto& [game_id, entry] : room.games) {
        const BotTable* table = CapableEngine<kEngineBots>(entry.state);
        if (table == nullptr) continue;
        const chess_play::GameState& game = table->game();
        const int seat = game.whoseTurn();
        if (seat < 0) continue;
        const std::string& on_turn = game.players().at(seat);
        const std::optional<int> elo = ChessBotElo(on_turn);
        if (!elo.has_value() || chess_bot_retry_at_.contains({room_id, game_id})) continue;
        // Movetime is filled just before the ask, from the seat's named
        // strength and the live remaining clock — not here — so a later
        // bot in this pass is not capped by a snapshot taken before
        // earlier bots thought. Elo stays off the ask (full strength).
        turns.push_back({room_id, game_id, on_turn, table->scoreSheet().size(),
                         ChessBotAsk{game.startFen(), game.moves(), 0, std::nullopt}});
      }
    }
  }
  // The engine is asked without mu_: it thinks for a movetime, and the
  // hub serves everyone else meanwhile.
  int played = 0;
  for (Turn& turn : turns) {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room = rooms_.find(turn.room_id);
      if (room == rooms_.end()) continue;
      const auto game = room->second.games.find(turn.game_id);
      if (game == room->second.games.end()) continue;
      const BotTable* table = CapableEngine<kEngineBots>(game->second.state);
      if (table == nullptr || table->scoreSheet().size() != turn.game_no) continue;
      const chess_play::GameState& state = table->game();
      if (state.startFen() != turn.ask.fen || state.moves().size() != turn.ask.moves.size()) {
        continue;
      }
      const int seat = state.whoseTurn();
      if (seat < 0 || state.players().at(seat) != turn.bot_id) continue;
      // Strength → think time; Elo is not put on the ask (full-strength
      // Stockfish). The side's remaining clock only caps the budget.
      const std::optional<int> strength = ChessBotElo(turn.bot_id);
      turn.ask.movetime_ms = ChessBotMovetimeMs(
          state.remainingMs(state.sideToMove(), NowMs()), strength.value_or(1320));
      turn.ask.elo = std::nullopt;
    }
    const auto ask_started = std::chrono::steady_clock::now();
    LOG(INFO) << "chess bot " << turn.bot_id << " in " << turn.room_id << "/" << turn.game_id
              << ": asking movetimeMs=" << turn.ask.movetime_ms
              << " moves=" << turn.ask.moves.size() << " fen=" << turn.ask.fen;
    const absl::StatusOr<std::string> answer = chess_bot_engine_(turn.ask);
    const auto ask_took = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ask_started);
    Outbox outbox;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const std::pair<std::string, std::string> key{turn.room_id, turn.game_id};
      if (!answer.ok()) {
        LOG(WARNING) << "chess bot " << turn.bot_id << " in " << turn.room_id << "/"
                     << turn.game_id << ": movetimeMs=" << turn.ask.movetime_ms << " after "
                     << ask_took.count() << "ms: " << answer.status();
        chess_bot_retry_at_[key] = {turn.game_no, NowMs() + kChessBotRetry.count()};
        continue;
      }
      const auto room = rooms_.find(turn.room_id);
      if (room == rooms_.end()) continue;
      const auto game = room->second.games.find(turn.game_id);
      if (game == room->second.games.end() || !game->second.started()) continue;
      const GameRef ref{turn.room_id, &room->second, turn.game_id, &game->second};
      const std::size_t asked_at = turn.ask.moves.size();
      // Only on the game it was asked about: the same game of the table,
      // the same start and as many moves, the bot still on turn. Anything
      // else moved on meanwhile. An answer that game refuses is the
      // engine's fault, and waits out the retry like a failed ask.
      bool moved_on = false;
      const auto refusal = ApplyTableEngineMoveLocked<BotTable>(
          ref, turn.bot_id,
          Transition<BotTable>{[&turn, &moved_on, asked_at, uci = *answer](
                                   const BotTable& table, int seat, const MoveContext& context) {
            if (table.scoreSheet().size() != turn.game_no) {
              moved_on = true;
              return absl::StatusOr<BotTable>(
                  absl::FailedPreconditionError("the position moved on"));
            }
            return table.inGame([&](const chess_play::GameState& state)
                                    -> absl::StatusOr<chess_play::GameState> {
              if (state.startFen() != turn.ask.fen || state.moves().size() != asked_at) {
                moved_on = true;
                return absl::FailedPreconditionError("the position moved on");
              }
              return state.move(seat, uci, context.now_ms);
            });
          }},
          outbox);
      if (refusal.has_value()) {
        LOG(INFO) << "chess bot " << turn.bot_id << " in " << turn.room_id << "/" << turn.game_id
                  << ": " << turn.ask.moves.size() << " moves in, uci=" << *answer << " after "
                  << ask_took.count() << "ms, not played: " << refusal->reason;
        if (!moved_on) chess_bot_retry_at_[key] = {turn.game_no, NowMs() + kChessBotRetry.count()};
      } else {
        LOG(INFO) << "chess bot " << turn.bot_id << " in " << turn.room_id << "/" << turn.game_id
                  << ": played " << *answer << " after " << ask_took.count()
                  << "ms (movetimeMs=" << turn.ask.movetime_ms << ")";
        ++played;
      }
    }
    Deliver(outbox);
  }
  return played;
}

void GamesHub::StartChessBots(std::chrono::milliseconds interval) {
  if (chess_bots_.joinable() || chess_bot_engine_ == nullptr) return;
  chess_bots_ = std::thread([this, interval] {
    while (true) {
      PlayChessBotsOnce();
      std::unique_lock<std::mutex> lock(chess_bot_mu_);
      chess_bot_cv_.wait_for(lock, interval, [this] { return chess_bot_stop_; });
      if (chess_bot_stop_) return;
    }
  });
}

void GamesHub::StartChessClocks(std::chrono::milliseconds interval) {
  if (chess_clocks_.joinable()) return;
  chess_clocks_ = std::thread([this, interval] {
    while (true) {
      SweepChessClocksOnce();
      std::unique_lock<std::mutex> lock(chess_clock_mu_);
      chess_clock_cv_.wait_for(lock, interval, [this] { return chess_clock_stop_; });
      if (chess_clock_stop_) return;
    }
  });
}

namespace {

// Which point of the table's evening a serve is: it picks the serve's line.
int ServeNo(const RallyTable& table) {
  return table.points() + static_cast<int>(table.scoreSheet().size());
}

moonbase::games::PongVector WireVector(const pong::Vec3& v) {
  moonbase::games::PongVector wire;
  wire.x = v.x;
  wire.y = v.y;
  wire.z = v.z;
  return wire;
}

}  // namespace

void GamesHub::PaddleMove(const std::string& player_id, const moonbase::games::PongPaddle& paddle) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const auto ref = FindGameLocked(player_id);
    if (ref.has_value() && HasCapability(ref->entry->kind, kRealtime)) {
      const GameEntry& entry = *ref->entry;
      const RallyTable* table = CapableEngine<kRealtime>(entry.state);
      // Between games a pointer still moves; there is nothing to steer.
      if (table == nullptr || !table->inPlay()) return;
      const int seat = table->playerIndex(player_id);
      if (seat < 0) return;
      const std::pair<std::string, std::string> key{ref->room_id, ref->game_id};
      // Another instance runs this rally: steering it there is beyond
      // this one.
      if (pong_elsewhere_.contains(key)) return;
      HostPongLocked(ref->room_id, ref->game_id, entry);
      pong::Vec2& target = pong_rallies_.at(key).targets[seat];
      // An x left out keeps the one before: the flat court has none.
      target = {paddle.x.value_or(target.x), paddle.y};
      return;
    }
  }
  Reject(player_id, RejectKind::kState, "not seated at a pong table");
}

void GamesHub::HostPongLocked(const std::string& room_id, const std::string& game_id,
                             const GameEntry& entry) {
  const RallyTable* in_play = CapableEngine<kRealtime>(entry.state);
  if (in_play == nullptr || !in_play->inPlay()) return;
  const auto [live, hosted] = pong_rallies_.try_emplace({room_id, game_id});
  if (!hosted) return;
  const RallyTable& table = *in_play;
  // Each game, the other seat receives first.
  const int receiver = table.scoreSheet().size() % 2 == 0 ? 1 : 0;
  live->second.rally = pong::Serve(table.variant(), receiver, ServeNo(table));
  live->second.targets = live->second.rally.paddles;
  // Ticks count on from the clock, so a rally taken up here, after a
  // restart or another instance, never draws a frame a client holds stale.
  live->second.first_tick = NowMs() * pong::kTicksPerSecond / 1000;
}

int GamesHub::TickPongOnce() {
  Outbox outbox;
  // Frames go out droppable, after the views: a slow reader keeps the
  // newest of each table's.
  std::vector<std::tuple<std::string, std::string, GameEvents>> frames;
  int ticked = 0;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // A point's commit can hand a rally away, so the walk is over the
    // rallies as they stood and each is looked up again.
    std::vector<std::pair<std::string, std::string>> keys;
    for (const auto& [key, live] : pong_rallies_) keys.push_back(key);
    for (const auto& key : keys) {
      const auto& [room_id, game_id] = key;
      const auto room = rooms_.find(room_id);
      GameEntry* entry = nullptr;
      if (room != rooms_.end()) {
        const auto game = room->second.games.find(game_id);
        if (game != room->second.games.end()) entry = &game->second;
      }
      const RallyTable* in_play =
          entry == nullptr ? nullptr : CapableEngine<kRealtime>(entry->state);
      if (in_play == nullptr || !in_play->inPlay()) {
        pong_rallies_.erase(key);
        continue;
      }
      PongRally& live = pong_rallies_.at(key);
      const RallyTable& table = *in_play;
      std::array<pong::Vec2, pong::kSeats> targets = live.targets;
      for (int seat = 0; seat < pong::kSeats; ++seat) {
        if (const auto level = PongBotLevel(table.players()[seat])) {
          targets[seat] = pong::BotTarget(live.rally, seat, *level);
        }
      }
      const pong::Advanced step = pong::Advance(live.rally, targets);
      live.rally = step.rally;
      const int64_t ticks = ++live.ticks;
      ++ticked;
      if (step.missed_by.has_value()) {
        const int missed = *step.missed_by;
        const pong::Variant variant = table.variant();
        const std::array<pong::Vec2, pong::kSeats> paddles = live.rally.paddles;
        HostedState next(*table.point(missed));
        std::optional<std::vector<HubStore::StatsDelta>> deltas =
            DealEndDeltas(*entry->state, next);
        // The commit settles who runs the rally on: this instance, while
        // the game is in play and the commit lands.
        const Commit commit = CommitEntryLocked(room_id, game_id, *entry, entry->roster, next,
                                                deltas.has_value() ? &*deltas : nullptr);
        if (commit == Commit::kGone) {
          DropGameLocked(GameRef{room_id, &room->second, game_id, entry}, outbox);
          StageRoomStateLocked(room_id, outbox);
          pong_rallies_.erase(key);
          continue;
        }
        if (commit == Commit::kUnavailable) {
          // Nothing stored the point, so it is played again.
          live.rally = pong::Serve(variant, missed, ServeNo(table), paddles);
        } else {
          // The views and the game's end, then the room's stats it moved.
          StageGameViewsLocked(game_id, *entry, outbox);
          if (commit == Commit::kCommitted && deltas.has_value()) {
            MirrorStatsLocked(room->second, *deltas);
            StageRoomStateLocked(room_id, outbox);
          }
          // A game this point ended has no next serve, and no frame.
          const RallyTable* after = CapableEngine<kRealtime>(entry->state);
          if (after == nullptr || !after->inPlay()) {
            pong_rallies_.erase(key);
            continue;
          }
          if (const auto served = pong_rallies_.find(key); served != pong_rallies_.end()) {
            served->second.rally = pong::Serve(variant, missed, ServeNo(*after), paddles);
          }
        }
      }
      const auto running = pong_rallies_.find(key);
      if (running == pong_rallies_.end()) continue;
      if (ticks % kPongFrameEvery != 0 && !step.missed_by.has_value()) continue;
      const pong::Rally& rally = running->second.rally;
      moonbase::games::PongFrame frame;
      frame.gameId = game_id;
      frame.tick = running->second.first_tick + ticks;
      frame.ball = WireVector(rally.ball);
      frame.velocity = WireVector(rally.velocity);
      for (const pong::Vec2& paddle : rally.paddles) {
        moonbase::games::PongPoint point;
        point.x = paddle.x;
        point.y = paddle.y;
        frame.paddles.push_back(point);
      }
      frame.serveIn = rally.serve_wait;
      const GameEvents event = RallyGame::Wrap(PongUpdate::FromFrame(frame));
      for (const std::string& recipient : entry->roster) {
        if (!PongBotLevel(recipient).has_value()) frames.emplace_back(recipient, game_id, event);
      }
      for (const std::string& watcher : entry->watchers) {
        frames.emplace_back(watcher, game_id, event);
      }
    }
  }
  Deliver(outbox);
  for (auto& [recipient, game_id, event] : frames) {
    Send(recipient, std::move(event),
         opal::server::DeliveryClass::Coalesce(absl::StrCat("pong:", game_id)));
  }
  return ticked;
}

void GamesHub::StartPongTicks() {
  if (pong_ticks_.joinable()) return;
  pong_ticks_ = std::thread([this] {
    const auto interval = std::chrono::microseconds(1'000'000 / pong::kTicksPerSecond);
    auto next = std::chrono::steady_clock::now();
    while (true) {
      TickPongOnce();
      next += interval;
      // A tick that ran long (a slow store under a point) is not made up:
      // the rally slows for a moment rather than racing.
      next = std::max(next, std::chrono::steady_clock::now());
      std::unique_lock<std::mutex> lock(pong_tick_mu_);
      pong_tick_cv_.wait_until(lock, next, [this] { return pong_tick_stop_; });
      if (pong_tick_stop_) return;
    }
  });
}

void GamesHub::SetConnected(const std::string& player_id, bool connected) {
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

std::optional<std::string> GamesHub::CurrentRoom(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return std::nullopt;
  return it->second;
}

bool GamesHub::WithMember(const std::string& room_id, const std::string& player_id,
                         const MemberAction& action) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return false;
  if (room->second.members.count(player_id) == 0) return false;
  action();
  return true;
}

GamesHub::Room* GamesHub::FindRoomLocked(const std::string& player_id) {
  const auto room_it = player_room_.find(player_id);
  if (room_it == player_room_.end()) return nullptr;
  const auto room = rooms_.find(room_it->second);
  return room != rooms_.end() ? &room->second : nullptr;
}

std::optional<GamesHub::GameRef> GamesHub::FindGameLocked(const std::string& player_id) {
  const auto room_it = player_room_.find(player_id);
  const auto game_it = player_game_.find(player_id);
  if (room_it == player_room_.end() || game_it == player_game_.end()) return std::nullopt;
  const auto room = rooms_.find(room_it->second);
  if (room == rooms_.end()) return std::nullopt;
  const auto game = room->second.games.find(game_it->second);
  if (game == room->second.games.end()) return std::nullopt;
  return GameRef{room_it->second, &room->second, game_it->second, &game->second};
}

void GamesHub::LeaveEverywhere(const std::string& player_id, Outbox& outbox, Writes& writes) {
  LeaveWorldLocked(player_id);
  LeaveVoiceLocked(player_id);
  StopWatchingLocked(player_id);
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
    world_.ForgetSurface(room_id);
    // Chat dies with its room. PostgreSQL gets this from the cascade on
    // the DeleteRoom below, but MemoryChatStore reclaims only here —
    // without this an emptied room keeps its last kChatHistoryLimit
    // messages for the life of the process.
    chat_store_->DropRoom(room_id);
    chat_cursors_.erase(room_id);
    // The room's last line, from the instance that emptied it and
    // not from the ones that later read the row gone — ReconcileRoomLocked
    // drops a remotely deleted room the same way, and a room closes once.
    RecordLocked([&room_id](absl::Time now) { return RoomClosedLine(now, room_id); });
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

void GamesHub::LeaveGameLocked(const std::string& player_id, Outbox& outbox, Writes& writes) {
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

    // A bot holds no table alone: with no player left, it goes too.
    const bool players_left = std::any_of(roster.begin(), roster.end(),
                                          [](const std::string& id) { return !IsBotId(id); });
    if (!entry.started() && !players_left) {
      DropWatchersLocked(ref->game_id, entry, outbox);
      for (const std::string& bot : roster) player_game_.erase(bot);
      const std::optional<EventTag> event = entry.event;
      ref->room->games.erase(ref->game_id);
      StageLocked(writes, HubStore::DeleteGame{ref->room_id, ref->game_id});
      StageWakeLocked(ref->room_id, writes);
      StageRoomStateLocked(ref->room_id, outbox);
      // Its pairing is at no table now.
      if (event.has_value()) {
        if (const auto held = ref->room->round_robins.find(event->event_id);
            held != ref->room->round_robins.end()) {
          StageRoundRobinLocked(*ref->room, held->second, outbox);
        }
      }
      return;
    }

    // With seats to spare the game continues compacted; otherwise the
    // engine ends it — golf keeps every seat so the departed hand still
    // reaches the scorecard (#1236), castle and rummy abandon with nobody
    // losing or winning. The
    // shrunken roster is what records who left — and who can still win.
    const std::optional<HostedState> state =
        entry.started() ? WithoutSeat(*entry.state, player_id, NowMs()) : std::nullopt;
    const std::string previous_turn = entry.started() ? CurrentTurnOf(*entry.state) : "";
    const bool over = state.has_value() && IsOver(*state);
    std::vector<HubStore::StatsDelta> deltas;
    if (over) deltas = StatsDeltasOf(*state, roster);
    // Terms are the poster's: a waiting table someone leaves has none,
    // unless they are its round robin's.
    const std::optional<ChessTerms> terms =
        entry.event.has_value() ? entry.terms : std::optional<ChessTerms>();
    const Commit commit = CommitEntryLocked(ref->room_id, ref->game_id, entry, roster, state,
                                            over ? &deltas : nullptr, &terms);
    if (commit == Commit::kRebased) continue;
    if (commit == Commit::kGone) {
      DropGameLocked(*ref, outbox);
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

void GamesHub::BroadcastRoom(const std::string& room_id) {
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    StageRoomStateLocked(room_id, outbox);
  }
  Deliver(outbox);
}

void GamesHub::Reject(const std::string& player_id, Refusal refusal) {
  Reject(player_id, refusal.kind, std::move(refusal.reason));
}

void GamesHub::Reject(const std::string& player_id, RejectKind kind, std::string reason) {
  // The bounded kind is the metric; the free text goes only to the player.
  // Reasons come from ~30 literals here and the cards engine's status
  // messages, exactly the label set that cannot be declared and so would
  // reopen #1323 for the rejection someone is actually looking for.
  Count("hub_rejections", {{"kind", RejectKindName(kind)}});
  moonbase::games::CommandRejected rejected;
  rejected.reason = std::move(reason);
  Send(player_id, GameEvents::FromCommandrejected(std::move(rejected)));
}

void GamesHub::OnExpired(const std::string& player_id) {
  // Grace ran out (ADR-0020): the seat is gone; free the room and game
  // slots and tell whoever remains. Runs on the registry's expiry thread.
  Count("hub_seats_expired");
  ReapUnlessResumedElsewhere(player_id);
}

bool GamesHub::ReapUnlessResumedElsewhere(const std::string& player_id) {
  Outbox outbox;
  Writes writes;
  bool reaped = false;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // Cross-instance grace: the player may have resumed
    // on another instance while parked here. One fresh read decides —
    // a member row back at connected belongs to its new instance. The
    // boot reaper leans on the same check from the other side: a row
    // still at connected after a whole boot grace may be a sibling's
    // live seat, so it stays (#1295 records the orphaned-row residue
    // this leaves — only that seat's owner could have known better).
    bool resumed_elsewhere = false;
    if (const auto room_it = player_room_.find(player_id); room_it != player_room_.end()) {
      // A copy: a refresh that finds the room deleted erases this entry.
      const std::string room_id = room_it->second;
      RefreshRoomLocked(room_id, outbox, /*project_always=*/true);
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

void GamesHub::Deliver(Outbox& outbox) {
  for (auto& [player_id, event] : outbox.events) {
    Send(player_id, std::move(event));
  }
  outbox.events.clear();
}

void GamesHub::PumpChat(const std::string& room_id) {
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

void GamesHub::SendChatHistory(const std::string& room_id, const std::string& player_id) {
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

void GamesHub::Count(const char* name, const std::map<std::string, std::string>& attributes) {
  if (metrics_) metrics_->RecordCounter(name, 1, attributes);
}

void GamesHub::TrackActive(int delta) {
  // Delta form, matching http_server_requests_active: the collector sums
  // an up-down counter into the live-session count.
  if (metrics_) metrics_->RecordGauge("hub_sessions_active", delta);
}

// The room layer's own cases count on hub_commands; each tenant's
// envelope counts its inner case on its own series (golf_*, castle_*,
// rummy_*, lobby_*, voice_*).
void GamesHub::CountCommand(const GameCommands& command) {
  if (!metrics_) return;
  const bool counted = AnyAdapter([&](auto adapter) {
    using A = decltype(adapter);
    const auto* move = A::MoveOf(command);
    if (move == nullptr) return false;
    metrics_->RecordCounter(std::string(A::kCommandSeries), 1,
                            {{"command", std::string(move->case_name())}});
    return true;
  });
  if (counted) return;
  if (const auto* lobby = command.as_lobby_or_null()) {
    metrics_->RecordCounter("lobby_commands", 1,
                            {{"command", std::string(lobby->action.case_name())}});
  } else if (const auto* voice = command.as_voice_or_null()) {
    metrics_->RecordCounter("voice_commands", 1,
                            {{"command", std::string(voice->action.case_name())}});
  } else {
    metrics_->RecordCounter("hub_commands", 1, {{"command", std::string(command.case_name())}});
  }
}

void GamesHub::Send(const std::string& player_id, GameEvents event,
                   opal::server::DeliveryClass delivery) {
  if (metrics_) {
    const bool counted = AnyAdapter([&](auto adapter) {
      using A = decltype(adapter);
      const auto* update = A::UpdateOf(event);
      if (update == nullptr) return false;
      metrics_->RecordCounter(std::string(A::kEventSeries), 1,
                              {{"event", std::string(update->case_name())}});
      return true;
    });
    if (counted) {
      // The game's own series.
    } else if (const auto* lobby = event.as_lobby_or_null()) {
      metrics_->RecordCounter("lobby_events", 1,
                              {{"event", std::string(lobby->update.case_name())}});
    } else if (const auto* voice = event.as_voice_or_null()) {
      metrics_->RecordCounter("voice_events", 1,
                              {{"event", std::string(voice->update.case_name())}});
    } else {
      metrics_->RecordCounter("hub_events", 1, {{"event", std::string(event.case_name())}});
    }
  }
  if (hooks_.on_send) hooks_.on_send(player_id, event, delivery);
  registry_.SendTo(player_id, std::move(event), std::move(delivery));
}

moonbase::games::RoomState GamesHub::RoomStateLocked(const std::string& room_id,
                                                    const Room& room) const {
  moonbase::games::RoomState state;
  state.roomId = room_id;
  state.geometry = GeometryOf(world_.SurfaceOf(room_id));
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
    if (entry.terms.has_value()) summary.terms = TermsLine(*entry.terms);
    state.games.push_back(std::move(summary));
  }
  return state;
}

void GamesHub::StageRoomStateLocked(const std::string& room_id, Outbox& outbox) const {
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return;
  const moonbase::games::RoomState state = RoomStateLocked(room_id, room->second);
  for (const auto& member : room->second.members) {
    outbox.To(member.first, GameEvents::FromRoomstate(state));
  }
}

TableContext GamesHub::ContextLocked(const std::string& game_id, const GameEntry& entry) const {
  return TableContext{game_id, entry.roster, entry.watchers, entry.terms, entry.event, NowMs()};
}

GameEvents GamesHub::JoinedEventLocked(const std::string& game_id, const GameEntry& entry,
                                      const std::string& viewer_id) const {
  return JoinedEventOf(entry.kind, ContextLocked(game_id, entry), entry.state, viewer_id);
}

void GamesHub::StageGameViewsLocked(const std::string& game_id, const GameEntry& entry,
                                   Outbox& outbox) const {
  Staged staged;
  entry.games_announced =
      StageViews(entry.kind, ContextLocked(game_id, entry), entry.state, entry.games_announced, staged);
  for (auto& [recipient, event] : staged) outbox.To(recipient, std::move(event));
  // A pairing's result follows the game's own ending.
  for (const auto& [room_id, round_robin_id] : round_robins_moved_) {
    const auto room = rooms_.find(room_id);
    if (room == rooms_.end()) continue;
    const auto row = room->second.round_robins.find(round_robin_id);
    if (row != room->second.round_robins.end()) {
      StageRoundRobinLocked(room->second, row->second, outbox);
    }
  }
  round_robins_moved_.clear();
}

// Final views (every card face up), with the result of a game the close
// ended, to the seats and any watcher, then whatever the game tells its
// seats of the close; the table is done and gone locally. A leaver hears
// none of it: the roster no longer names it, and its gameLeft said it was
// out.
void GamesHub::StageGameOverLocked(Room& room, const std::string& game_id, Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.started()) return;
  const GameEntry& entry = game->second;
  StageGameViewsLocked(game_id, entry, outbox);
  Staged staged;
  StageClosedNotice(entry.kind, ContextLocked(game_id, entry), entry.state, staged);
  for (auto& [recipient, event] : staged) outbox.To(recipient, std::move(event));
  for (const std::string& recipient : entry.roster) player_game_.erase(recipient);
  room.games.erase(game);
}

void GamesHub::MirrorStatsLocked(Room& room, const std::vector<HubStore::StatsDelta>& deltas) {
  for (const HubStore::StatsDelta& delta : deltas) {
    const auto member = room.members.find(delta.player_id);
    if (member == room.members.end()) continue;
    member->second.games_played += delta.played;
    member->second.games_won += delta.won;
    member->second.total_score += delta.score;
  }
}

void GamesHub::FinalizeGameLocked(const std::string& room_id, Room& room, const std::string& game_id,
                                 Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.started()) return;

  // Room-scoped running stats: every roster seat played, every winner
  // won. These same deltas rode the finish commit, or its fate is unknown
  // after a kUnavailable leave; either way this mirrors them locally.
  MirrorStatsLocked(room, StatsDeltasOf(*game->second.state, game->second.roster));

  StageGameOverLocked(room, game_id, outbox);
  // The terminal row is the durable handoff to other instances. It
  // remains until the room is deleted and its foreign-key cascade runs;
  // deleting it here can outrun a listener that has not handled the
  // finish wake yet.
  StageRoomStateLocked(room_id, outbox);
}

}  // namespace games_hub
