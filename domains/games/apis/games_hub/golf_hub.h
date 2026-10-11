#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_HUB_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GOLF_HUB_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/ai/libs/deja_cpp/client.h"
#include "domains/games/apis/games_hub/chat_store.h"
#include "domains/games/apis/games_hub/chess_bots.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/apis/games_hub/hub_metrics.h"
#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/id_generator.h"
#include "domains/games/apis/games_hub/move_coalescing.h"
#include "domains/games/apis/games_hub/rate_limiter.h"
#include "domains/games/apis/games_hub/room_bot.h"
#include "domains/games/apis/games_hub/ticket_vault.h"
#include "domains/games/apis/games_hub/voice.h"
#include "domains/games/apis/games_hub/wordchain.h"
#include "domains/games/apis/games_hub/world.h"
#include "domains/games/libs/cards/castle/game_state.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/cards/rummy/table.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/table.h"
#include "domains/games/libs/one_d4_cpp/index_queue.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/pg/listener.h"
#include "moonbase/games/server.h"
#include "opal/server/session_registry.h"

namespace games_hub {

/// winners() restricted to the seats the roster still names — the
/// forfeit rule. A final state can carry a seat its roster has dropped:
/// an abandonment that ended the game keeps the departed hand so its
/// cards and score reach the scorecard (#1236), and such a seat cannot
/// win — however good its standing score, the game resolved against it.
/// For an ordinary finish the roster names every seat and this is
/// exactly the engine's rule, knocker-takes-ties included.
[[nodiscard]] std::unordered_set<int> WinnersAmong(const golf::GameState& state,
                                                   const std::vector<std::string>& roster);

/// Scheduling seams for the race tests, unset in production. A hook runs
/// on the hub's thread and must not call into the hub; each says whether
/// it holds mu_.
struct GolfTestHooks {
  /// Outside mu_, on the close path, after the world entry is gone and
  /// playerLeft has fanned out, before the seat parks for a resume to
  /// reclaim.
  std::function<void(const std::string& player_id)> before_seat_release;
  /// Under mu_, after a lobby command's world deliveries have gone to the
  /// registry, before the lock is released.
  std::function<void()> after_world_sent;
  /// For each event as it goes to the registry, with the delivery class
  /// it rides. Under mu_ for the world's and voice's deliveries.
  std::function<void(const std::string& to, const moonbase::games::GameEvents& event,
                     const opal::server::DeliveryClass& delivery)>
      on_send;
};

/// GolfHub is the room hub (#1187): seat admission, rooms,
/// chat, and the game layer, behind GamesHubHandler::Play. The name is
/// golf's; the room layer, castle (#77), rummy (#245), chess and the lobby
/// (#1490) live here too. A room hosts tables of any game (#79): golf on
/// libs/cards/golf, castle on libs/cards/castle, rummy on libs/cards/rummy
/// and chess on libs/chess_play, each a member of the stream's unions with
/// its own per-viewer view. Each tenant's envelope counts on its own series
/// (golf_, castle_, rummy_, chess_, lobby_, voice_); the room layer's own are
/// hub_*.
///
/// The lobby member is the World (world.h) keyed by the session's room:
/// a roomed session stands in its room's world, an unroomed one in the
/// plaza's. Presence is the socket — a close leaves the world at once
/// while the seat parks for grace — and the room: joining, creating, or
/// leaving a room leaves whatever world the session stood in, and the
/// client joins the new one. The world lives under mu_ with the rooms,
/// and its deliveries go to the registry under mu_ too — the registry
/// only queues — so a joiner's snapshot is out before any later command
/// can change what it shows, and a leave that races a join lands behind
/// the snapshot that lists the leaver. One SessionRegistry
/// keyed by playerId carries all fan-out (async delivery — no writer
/// threads); rooms are a mutex'd map, membership marked disconnected
/// during ADR-0020 grace and reaped by on_expired — with one boot-time
/// grace for restored members no session ever reclaims (#1295), since
/// the registry that died with the old process took their timers.
///
/// Redaction discipline: every game broadcast is staged per recipient
/// (StageGameViewsLocked, JoinedEventLocked) with views built by each
/// game's ViewLocked/CastleViewLocked/RummyViewLocked — per-viewer state
/// (golf's own peeks and held draw, castle's and rummy's own hand faces)
/// has exactly one place per game
/// to land and no identical-bytes path can leak it.
///
/// Chat observability (#1226): chat_appends{result}, chat_rows_delivered,
/// chat_catch_up_drains (one per drain, empty drains included, so
/// rate(rows)/rate(drains) is how far behind a wake found this instance —
/// the lag signal), chat_history_replays, and chat_failures{stage}. Counts
/// and stages only: room ids, player ids, and message text never reach a
/// metric name or label, and the e2e suite sweeps for exactly that.
///
/// With a store attached, instances are interchangeable (#1194):
/// the database is every game's authority. A move loads nothing extra —
/// the local entry mirrors the stored row — but its result only counts
/// once the conditional commit lands; a miss rebases the entry from the
/// stored truth and retries the transition. Each commit's NOTIFY wakes
/// the other instances holding that room; a woken instance re-reads the
/// rows and re-projects views for its local players (redaction stays
/// local — only wake-ups cross the wire). Rooms and members ride the
/// async write-through (single writer per row, nothing to
/// conflict with), with notify riders so remote rosters converge. Chat
/// commits to its own ChatStore before it is echoed, so a message its
/// sender sees is a message that was stored; remote appends reach local
/// members through PumpChat on the room's chat wake, and joiners get
/// SendChatHistory.
class GolfHub final {
 public:
  using Registry = opal::server::SessionRegistry<moonbase::games::GameEvents>;

  /// Takes one game event: the moment it happened, and the line
  /// (game_events.h) that records it. The time is handed over rather
  /// than read again downstream, so the file an event rolls into and
  /// the timestamp inside it can never disagree.
  using EventWriter = std::function<void(absl::Time when, std::string_view line)>;

  /// One counter series, name and exact attributes (hub_metrics.h).
  using CounterSeries = games_hub::CounterSeries;

  /// Every counter series this hub baselines at construction, listing
  /// each label value its emit sites actually use — every counter this
  /// hub emits is on it, with no carve-outs (#1384, #1327).
  ///
  /// The label values are here rather than inferred because a series is
  /// name *and* attributes: declaring a bare `chat_appends` when every emit
  /// site writes `chat_appends{result=...}` baselines an orphan nothing ever
  /// increments, and leaves the three real series to be born carrying their
  /// first event's value — which is the bug (#1323), not a fix for it.
  ///
  /// hub_commands{command} and hub_events{event} take the case names of
  /// games.smithy's stream unions, golf_commands and golf_events those of
  /// golf.smithy's, so their blocks are a copy of the model.
  /// StreamSeriesMatchTheModelUnions parses the .smithy file and fails on
  /// any drift, in either direction — that test is what makes a hand-kept
  /// copy tolerable. The generated unions already hold this list (the
  /// kNames array behind case_name()); a opal-cpp accessor exposing it
  /// would let this block be derived and the parser test deleted.
  ///
  /// hub_rejections carries the bounded `kind` (see RejectKind), never
  /// the free-text reason: the reason strings are ~30 literals spread across
  /// this file and the cards engine, exactly the label set that rots.
  ///
  /// Exposed so the tests can assert the emit sites declare nothing this list
  /// does not name.
  static const std::vector<CounterSeries>& DeclaredCounterSeries();

  /// Every rooms/members/games mutation goes through HubStore. A null
  /// argument selects MemoryHubStore (main's no-database mode); PostgreSQL
  /// callers inject PgHubStore. Call RestoreFromStore before serving to
  /// rebuild rooms, members, and games.
  ///
  /// Chat has its own store because its write path shares nothing with
  /// the others. A null chat_store selects a MemoryChatStore authorized
  /// through WithMember below: process-local, so it neither survives a
  /// restart nor reaches another instance. A
  /// PgChatStore passed here authorizes against room_members in its own
  /// transaction and needs nothing from this handler.
  /// `limits` are the per-session stream budgets (#1240); the defaults
  /// serve production and tests inject extremes to pin the behavior.
  explicit GolfHub(std::shared_ptr<TicketVault> vault,
                   std::shared_ptr<cards::Dealer> dealer = std::make_shared<cards::Dealer>(),
                   std::shared_ptr<IdGenerator> ids = std::make_shared<WhimsicalIdGenerator>(),
                   std::chrono::seconds grace_period = std::chrono::minutes(5),
                   std::shared_ptr<futility::otel::MetricsRecorder> metrics = nullptr,
                   std::shared_ptr<HubStore> store = nullptr,
                   std::shared_ptr<ChatStore> chat_store = nullptr, RateLimits limits = {},
                   GolfTestHooks hooks = {});

  /// Joins the boot reaper before the members it walks are torn down.
  ~GolfHub();

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

  /// The golf stream, on GamesHubHandler::Play's signature: spend the
  /// ticket, admit the seat, sessionReady and the room resync, then
  /// commands until the socket closes.
  opal::eventstream::StreamTask Play(moonbase::games::PlayInput input,
                                     moonbase::games::PlayAsyncServerStream& stream);

  /// For main's SIGTERM path: Drain, then transport Stop.
  Registry& registry() { return registry_; }

  /// Readers the world's move coalescing tracks; for tests.
  std::size_t MoveCoalescingReaders() {
    const std::lock_guard<std::mutex> lock(mu_);
    return move_coalescing_.readers();
  }

  /// Boot-time restore of the store's snapshot: rooms, members (presence
  /// seeded from their rows), and games. Call before the
  /// transport serves. Undecodable rows were already dropped (loudly) by
  /// the store — an error here means the snapshot couldn't be read, or
  /// restore already ran.
  absl::Status RestoreFromStore();

  /// Wires the fan-out's LISTEN side (#1194): subscribes every
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
  /// Off-lock room reads a catch-up tries before it reads under the lock.
  static constexpr int kCatchUpReadAttempts = 3;

  /// Wires deja's tape onto the glasshouse walls (#1554, #1150) and
  /// starts polling for it. The hub is deja's second consumer and the
  /// tape is world state it owns: it polls, dedupes, picks the splat
  /// point, and fans out; no browser ever talks to deja. Call before
  /// serving. The thread is PollTapeOnce on a one-to-two-second jittered
  /// tick, and with no glasshouse occupied it reaches no network at all.
  /// A second call changes nothing — the running thread reads the client
  /// without a lock, so it is not swapped underneath.
  void StartTapePolling(std::shared_ptr<deja::Client> tape);

  /// Where finished games are recorded for the stats pipeline (#1571):
  /// one line per game that ended, written under mu_ on the finishing
  /// instance only. Unset — the default, and what every test that does
  /// not care gets — writes nothing at all; main sets it when
  /// GAME_EVENT_LOG_DIR names a directory. Call before serving.
  ///
  /// A line sink rather than the event struct, so a test sees the text
  /// that would ship rather than an intermediate nobody archives.
  void SetEventWriter(EventWriter writer);

  /// The ICE servers every voice roster hands a joiner (#1590). Call
  /// before serving; none leaves browsers to their host candidates.
  void SetIceServers(std::vector<moonbase::games::IceServer> servers);

  /// Starts the room bot (#1591): "@bot" mentions in room chat are
  /// answered by microgpt through `client`. main starts it when
  /// MICROGPT_URL is set; unstarted, a mention is ordinary chat. Call
  /// once, before serving.
  void StartRoomBot(std::shared_ptr<microgpt::Client> client, BotLimits limits = {});

  /// Starts the wordchain bot: "/wordchain start end" in room chat is
  /// answered by mithril through `client`. main starts it when MITHRIL_URL
  /// is set; unstarted, the command is ordinary chat. Call once, before
  /// serving.
  void StartWordchain(std::shared_ptr<mithril::Client> client, BotLimits limits = {});

  /// Starts the room heartbeat: StampHeldRooms then SweepStaleRooms every
  /// `interval` until the hub is destroyed. main starts it when rooms persist; without a
  /// store there is no fleet and nothing to vouch to. A second call
  /// changes nothing.
  void StartRoomHeartbeat(std::chrono::milliseconds interval = kRoomHeartbeat);

  /// Vouches for every room this instance holds a seat in, live or parked
  /// within its grace, with one TouchRooms write. A room this instance
  /// only knows from rows — a crashed instance's ghost among them — is
  /// not stamped, so its last_active_at ages toward the sweep.
  void StampHeldRooms();

  /// Deletes the rooms no instance has stamped for kRoomStaleAfter — the
  /// ones only a crashed instance held — and the published chess games
  /// older than kPublishedChessKept. Every instance sweeps; the deletes
  /// are idempotent, and the room sweep's wake drops each swept room
  /// wherever it is still held.
  void SweepStaleRooms();

  /// How long a published chess game stays in the public feed (#1637).
  static constexpr std::chrono::hours kPublishedChessKept{30 * 24};

  /// How often the heartbeat stamps and sweeps.
  static constexpr std::chrono::milliseconds kRoomHeartbeat{60000};
  /// How long a room may go unstamped before the sweep takes it: sixty
  /// missed heartbeats, so an instance has to be gone, not just slow.
  static constexpr std::chrono::milliseconds kRoomStaleAfter = 60 * kRoomHeartbeat;

  /// The client without the thread, for tests that drive PollTapeOnce
  /// themselves so no assertion waits on a clock.
  void AttachTape(std::shared_ptr<deja::Client> tape);

  /// One poll cycle. Returns whether deja was asked at all: false means
  /// nobody is standing in a glasshouse, and not a byte left the process.
  ///
  /// Called from the poll thread, or from a test, but from exactly one at
  /// a time — it owns tape_seq_ and the priming flag without a lock. mu_
  /// is taken twice and held across neither the round trip nor the
  /// decode, the same rule CatchUpRoom follows: deja slow, deja down, or
  /// deja talking nonsense must not reach the hub's lock or a socket.
  bool PollTapeOnce();

  /// Where a chess table starts, asked once per game at startGame. The
  /// default resolves the requested server-owned setup; tests fix its
  /// position. Call before serving: read without a lock thereafter.
  using ChessOpener =
      std::function<absl::StatusOr<chess_play::ChessSetup>(std::string_view setup_id)>;
  void SetChessOpener(ChessOpener opener);

  /// The wall clock chess's clocks read, absl::Now unless a test fixes
  /// it. Call before serving: read without a lock thereafter.
  void SetClock(std::function<absl::Time()> clock);

  /// Ends on time every chess game this instance holds whose side to
  /// move has run out at the clock's now, through the same conditional
  /// commit a move takes: with several instances holding the room, one
  /// lands the ending and the rest rebase onto it. Returns how many this
  /// call ended.
  int SweepChessClocksOnce();

  /// Chess games whose flag the store refused and that wait to be tried
  /// again; for tests.
  std::size_t ChessFlagRetriesPending() {
    const std::lock_guard<std::mutex> lock(mu_);
    return chess_flag_retry_at_.size();
  }

  /// Starts SweepChessClocksOnce on a thread every `interval` until the
  /// hub is destroyed. A second call changes nothing.
  void StartChessClocks(std::chrono::milliseconds interval = kChessClockTick);

  /// The engine chess bots ask (#1618); null, the default, seats no bot.
  /// Call before serving: read without a lock thereafter.
  void SetChessBotEngine(ChessBotEngine engine);

  /// Asked, for each human player, when this instance ends a chess game in
  /// a published room: 1d4 indexes them from the public feed. Called under
  /// the hub's lock, so it must not block (one_d4::IndexQueue::Submit). Null,
  /// the default, asks nothing.
  using ChessIndexer = std::function<void(const one_d4::IndexAsk&)>;
  void SetChessIndexer(ChessIndexer indexer);

  /// Plays the move of every chess bot this instance holds that is on
  /// turn: the position read under the lock, the engine asked without it
  /// for ChessBotMovetimeMs of the seat's named strength (capped by the
  /// side's remaining clock; no UCI Elo), and the answer played through
  /// the same conditional commit as a player's move — only if the game
  /// is still where it was asked. An engine that fails is asked again
  /// for that game after kChessBotRetry, the bot's clock running
  /// meanwhile. Returns how many moves it played.
  int PlayChessBotsOnce();

  /// The public chess feed past `after_archive_id` as one PGN archive, at
  /// most kChessHistoryLimit games (#1637). Reads the store only, so any
  /// instance answers.
  absl::StatusOr<std::string> ExportChessPgn(int64_t after_archive_id);

  /// One game of the public feed by its archive id, for its page; nullopt
  /// when the feed has none by that id. Reads the store only.
  absl::StatusOr<std::optional<moonbase::games::ChessReview>> PublishedChessReview(
      int64_t archive_id);

  /// Starts PlayChessBotsOnce on a thread every `interval` until the hub
  /// is destroyed. A second call changes nothing.
  void StartChessBots(std::chrono::milliseconds interval = kChessBotTick);

  /// How soon after its turn begins a bot starts to think.
  static constexpr std::chrono::milliseconds kChessBotTick{100};

  /// How late a flag may land after the time ran out.
  static constexpr std::chrono::milliseconds kChessClockTick{250};
  /// How long a flag the store could not take waits before the sweep
  /// tries that game again.
  static constexpr std::chrono::milliseconds kChessFlagRetry{5000};
  /// A chess clock the starter did not name.
  static constexpr chess_play::TimeControl kDefaultChessClock{180'000, 2'000};
  /// A room's round robins (#1647) live as long as it does; this many is
  /// an evening's worth, and a bound on what every wake reads.
  static constexpr std::size_t kMaxRoundRobinsPerRoom = 16;

  /// The tick between polls. deja scores roughly a request a second, so
  /// this is "about as often as there is something to show"; the jitter
  /// keeps a fleet of instances off a single second.
  static constexpr std::chrono::milliseconds kTapePollMin{1000};
  static constexpr std::chrono::milliseconds kTapePollMax{2000};

 private:
  void StartBot(std::shared_ptr<Responder> responder, BotLimits limits);

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
  /// is what serializes instances. kind is fixed at creation and says
  /// which engine the state is; golf()/castle()/rummy() are the typed
  /// reads.
  struct GameEntry {
    GameKind kind = GameKind::kGolf;
    std::vector<std::string> roster;
    std::optional<HostedState> state;
    int64_t version = 0;
    /// A chess table's finished games this instance has sent gameEnded
    /// for: whichever path moved the table on, the views staged after it
    /// announce the rest. Held from a row, the games so far count as told.
    mutable std::size_t chess_games_announced = 0;
    /// Members watching this chess table from no seat (#1633): they hear
    /// its views and results. Presence, like the world: held here, never
    /// stored. A table that ends hands them its closed view; one erased
    /// without it hands them gameLeft (DropWatchersLocked).
    std::set<std::string> watchers;
    /// A waiting chess table's posted challenge (#1633), stored with the
    /// row until the game starts on it.
    std::optional<ChessTerms> terms;
    /// The round robin pairing the table plays (#1647), fixed when it is
    /// opened.
    std::optional<EventTag> event;
    [[nodiscard]] bool started() const { return state.has_value(); }
    [[nodiscard]] const golf::GameState& golf() const { return std::get<golf::GameState>(*state); }
    [[nodiscard]] const castle::GameState& castle() const {
      return std::get<castle::GameState>(*state);
    }
    [[nodiscard]] const rummy::TableState& rummy() const {
      return std::get<rummy::TableState>(*state);
    }
    [[nodiscard]] const chess_play::Table& chess() const {
      return std::get<chess_play::Table>(*state);
    }
  };

  struct Room {
    std::map<std::string, Member> members;
    std::map<std::string, GameEntry> games;
    /// Stamp of the last local change to what this instance holds for
    /// the room: a write to its rows (member upserts and drops, game
    /// commits and deletes) or a reconcile that adopted rows. Drawn from
    /// one hub-wide sequence, so a room dropped and held again never
    /// repeats a stamp. A catch-up that read the rows off mu_ compares
    /// it before and after: a change in between means the rows may
    /// predate local truth, and reconciling them would roll the room
    /// back to the moment of the read.
    uint64_t revision = 0;
    /// Whether the room publishes its chess games (#1637), as its row
    /// last said; a reconcile that finds it changed tells the members.
    bool chess_published = false;
    /// The room's round robins (#1647) by id, as their rows last said; a
    /// reconcile that finds one changed tells the members.
    std::map<std::string, HubStore::ChessEventRow> round_robins;
  };
  /// Round robins whose games a commit just re-read, as (room, id): the
  /// next StageGameViewsLocked tells their rooms. Under mu_.
  mutable std::vector<std::pair<std::string, std::string>> round_robins_moved_;

  /// Events staged under the lock, delivered outside it. Delivery
  /// preserves staged order per recipient — callers stage in the order
  /// clients must observe (e.g. final views before gameEnded).
  struct Outbox {
    std::vector<std::pair<std::string, moonbase::games::GameEvents>> events;
    void To(const std::string& player_id, moonbase::games::GameEvents event) {
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
  /// A move of castle's or rummy's: the engine's next state, from the
  /// mover's seat.
  template <typename Engine>
  using TableMoveFn = std::function<absl::StatusOr<Engine>(const Engine&, int seat)>;
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

  void HandleCommand(const std::string& player_id, const moonbase::games::GameCommands& command);
  void HandleMove(const std::string& player_id, const moonbase::games::GolfMove& move);
  void HandleCastleMove(const std::string& player_id, const moonbase::games::CastleMove& move);
  void HandleRummyMove(const std::string& player_id, const moonbase::games::RummyMove& move);
  void HandleChessMove(const std::string& player_id, const moonbase::games::ChessMove& move);
  /// A bot to the second seat of `player_id`'s chess table (#1618).
  void AddChessBotMove(const std::string& player_id, int elo);
  /// The table's one seat posts the terms it starts on (#1633).
  void ChallengeChessMove(const std::string& player_id, ChessTerms terms);
  /// After a seat fills `player_id`'s table: a challenge starts on its
  /// terms. `terms` was read under the lock that seated it.
  void StartChallengeMove(const std::string& player_id, const std::optional<ChessTerms>& terms);
  /// `player_id` watches a chess table in its room from no seat (#1633).
  void WatchChessMove(const std::string& player_id, const std::string& game_id);
  /// The room's finished chess games, its review of one, and publishing
  /// them (#1637). The archive is read off the hub's lock.
  void ChessHistoryMove(const std::string& player_id);
  void ChessReviewMove(const std::string& player_id,
                       const moonbase::games::ChessReviewRequest& review);
  void PublishChessMove(const std::string& player_id, bool published);
  /// Round robins (#1647): any member creates one, its creator withdraws
  /// entrants and records forfeits, and any member asks for the room's.
  void CreateRoundRobinMove(const std::string& player_id,
                            const moonbase::games::ChessCreateRoundRobin& create);
  void WithdrawMove(const std::string& player_id, const moonbase::games::ChessWithdraw& withdraw);
  void ForfeitMove(const std::string& player_id, const moonbase::games::ChessForfeit& forfeit);
  void RoundRobinsMove(const std::string& player_id);
  /// Opens a table for the player's pairing with `opponent` (#1647).
  void PlayRoundRobinMove(const std::string& player_id,
                          const moonbase::games::ChessPlayRoundRobin& play);
  /// The tables playing a round robin's pairings now, by pairing index.
  std::map<int, std::string> LiveTablesLocked(const Room& room,
                                              const std::string& round_robin_id) const;
  /// Whether a chess table's game is over: ended between games or closed.
  static bool ChessGameOver(const GameEntry& entry);
  /// The pairing a tagged table plays, if its round robin is held.
  const Pairing* PairingOfLocked(const Room& room, const GameEntry& entry) const;
  /// After a commit ends a tagged table's game: the round robins' games
  /// re-read from the archive, their views staged with the next game
  /// views. Callers hold mu_.
  void RefreshRoundRobinGamesLocked(const std::string& room_id);
  /// The creator's `change` to one of the player's room's round robins,
  /// committed on its version and retried over a sibling's; every member
  /// held here hears the result. While the creator is away an entrant
  /// moderates, unless among the change's `parties`. Callers hold mu_.
  std::optional<games_hub::Refusal> ModerateRoundRobinLocked(
      const std::string& player_id, const std::string& round_robin_id,
      const std::vector<std::string>& parties,
      const std::function<std::optional<games_hub::Refusal>(HubStore::ChessEventRow&)>& change,
      Outbox& outbox);
  /// Tells every member of `room` held here how a round robin stands.
  void StageRoundRobinLocked(const Room& room, const HubStore::ChessEventRow& row,
                             Outbox& outbox) const;
  /// Tells every member of `room` held here that its games are published
  /// or withdrawn, by `by` when this instance knows who.
  void StagePublishedLocked(const Room& room, bool published, const std::optional<std::string>& by,
                            Outbox& outbox) const;
  /// Out of whatever table the player watches, in its room; callers hold
  /// mu_. The table watched, if any. Every way of sitting down or leaving
  /// the room comes through here first.
  std::optional<std::string> StopWatchingLocked(const std::string& player_id);
  /// The lifecycle half of castle's, rummy's and chess's move unions, which
  /// share its shapes (chess's startGame aside, which HandleChessMove takes
  /// first): true when `move` was one and has been handled.
  template <typename Move>
  bool LifecycleMove(const std::string& player_id, const Move& move, GameKind kind);
  /// The lobby member: the session's world is its room's, or the plaza's.
  void HandleLobby(const std::string& player_id, const moonbase::games::LobbyAction& action);
  /// The world key a session stands in, or would: its room, else the plaza.
  std::string WorldOfLocked(const std::string& player_id) const;
  /// Stages the world's deliveries as lobby events; callers hold mu_.
  void SendWorldLocked(World::Deliveries& deliveries);
  /// Out of whatever world the session stood in, the rest of it told;
  /// callers hold mu_. Shared by every way of leaving: the close path,
  /// LeaveEverywhere, and a room change.
  void LeaveWorldLocked(const std::string& player_id);
  /// The voice member: voice is the session's room's.
  void HandleVoice(const std::string& player_id, const moonbase::games::VoiceAction& action);
  /// Stages voice's deliveries as voice events; callers hold mu_.
  void SendVoiceLocked(Voice::Deliveries& deliveries);
  /// Out of the room's voice, the rest of it told; callers hold mu_.
  /// Wherever a session leaves its world, it leaves voice too.
  void LeaveVoiceLocked(const std::string& player_id);
  /// The lifecycle moves both games share. Create and join are told
  /// which game's envelope asked — a golf join of a castle table is
  /// refused, so nobody is seated at a table whose vocabulary they do not
  /// speak; start and leave read the table's kind. The room-wide
  /// gameCreated is the one event that crosses: a room hears every table
  /// in that table's own envelope.
  void CreateGameMove(const std::string& player_id, GameKind kind);
  void JoinGameMove(const std::string& player_id, const std::string& game_id, GameKind kind);
  /// `time_control` is chess's clock, and nothing to any other game.
  void StartGameMove(
      const std::string& player_id, chess_play::TimeControl time_control = kDefaultChessClock,
      std::optional<chess_play::ChessSetup> chess_setup = std::nullopt);
  /// The shared shape of every in-game engine move: transition, then
  /// stage the fan-out (views, turn change, game end) the result implies.
  void EngineMove(const std::string& player_id, const MoveFn& move, MoveEffects effects);
  /// Castle's and rummy's in-game moves: the same commit loop, and the
  /// engine's own turn order decides the turnChanged. `kind` is the game
  /// `Engine` plays; a move on another game's table is refused.
  template <typename Engine>
  void TableEngineMove(const std::string& player_id, GameKind kind,
                       const TableMoveFn<Engine>& move);
  /// TableEngineMove's commit loop on a resolved game, as `player_id`'s
  /// seat: the refusal if there is one, and otherwise the fan-out staged.
  /// The chess clock sweep calls it for the seat on turn, which sent
  /// nothing and so is told nothing of a refusal.
  template <typename Engine>
  std::optional<games_hub::Refusal> ApplyTableEngineMoveLocked(GameRef ref,
                                                               const std::string& player_id,
                                                               GameKind kind,
                                                               const TableMoveFn<Engine>& move,
                                                               Outbox& outbox);
  /// Epoch milliseconds on the chess clock.
  int64_t NowMs() const;

  /// Stream-side observability (#1187): the aura chain instruments
  /// only unary requests, so admissions, live-session count, disconnects,
  /// and the command/event flow are counted here. All no-ops when no
  /// recorder is injected.
  void Count(const char* name, const std::map<std::string, std::string>& attributes = {});

  /// Declares every series in DeclaredCounterSeries() at construction so
  /// each exports a zero baseline before it counts anything.
  void DeclareMetrics();
  void TrackActive(int delta);
  void CountCommand(const moonbase::games::GameCommands& command);
  /// Every event leaves through here so golf_events sees each send.
  void Send(const std::string& player_id, moonbase::games::GameEvents event,
            opal::server::DeliveryClass delivery = {});

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

  /// The bounded label on hub_rejections{kind} (hub_metrics.h).
  using RejectKind = games_hub::RejectKind;

  /// The flows that work under mu_ and Reject after releasing it stage
  /// one of these (hub_metrics.h).
  using Refusal = games_hub::Refusal;
  void Reject(const std::string& player_id, RejectKind kind, std::string reason);
  void Reject(const std::string& player_id, Refusal refusal);
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
  /// The poll thread's body: PollTapeOnce on the jittered tick above.
  void TapePollerMain();
  void Deliver(Outbox& outbox);
  /// Hands staged ops to the store's writer queue. Callers hold mu_ (see
  /// Writes above). Asynchronous — clients may be told before the row
  /// lands. Always leaves writes empty.
  void EnqueueWritesLocked(Writes& writes);
  /// Stamps the room with the next revision (see Room::revision). A
  /// room this instance no longer holds is skipped.
  void TouchRoomLocked(const std::string& room_id);

  /// Write-through staging; callers hold mu_.
  void StageLocked(Writes& writes, HubStore::Op op) const;
  void StageMemberLocked(const std::string& room_id, const std::string& player_id,
                         const Member& member, Writes& writes) const;
  /// The async batch's wake-up rider: remote instances holding the room
  /// refresh once the writes staged before it have landed.
  void StageWakeLocked(const std::string& room_id, Writes& writes) const;

  /// One synchronous conditional commit of the entry's next revision
  /// (#1194). kCommitted adopts the candidate roster/state at version+1
  /// (a MemoryHubStore only this hub writes cannot lose the race; tests
  /// share one across hubs to drive kRebased). kRebased means
  /// another instance committed first: the entry now holds the stored
  /// truth — revalidate and retry. kGone: the game vanished remotely
  /// (the entry is untouched; the caller drops it). kUnavailable: the
  /// commit's fate is unknown; nothing was adopted.
  enum class Commit { kCommitted, kRebased, kGone, kUnavailable };
  /// `terms` is a waiting chess table's challenge to commit; null keeps
  /// the entry's. A started row carries none.
  Commit CommitEntryLocked(const std::string& room_id, const std::string& game_id, GameEntry& entry,
                           const std::vector<std::string>& roster,
                           const std::optional<HostedState>& state,
                           const std::vector<HubStore::StatsDelta>* finish,
                           const std::optional<ChessTerms>* terms = nullptr);

  /// Shared chat/room dispatch for OnNotify and OnChannelActive.
  /// `from_active` skips the own-instance filter (active has no payload)
  /// and uses change-gated projection for rooms so reconnect catch-up
  /// does not re-fan identical state.
  void WakeChannel(const std::string& channel, const std::string& payload, bool from_active);

  /// Notify/active catch-up for a held room: Flush+LoadRoom off mu_
  /// (PumpChat's pattern), then reconcile under the lock. Keeps the
  /// listener poll thread from holding mu_ across DB round trips on a
  /// reconnect storm. Rows read while the room changed locally are not
  /// trusted: the read is retried, and a room that keeps moving is
  /// re-read under the lock.
  void CatchUpRoom(const std::string& room_id, bool project_always);

  /// The under-lock read: flush our own queue (so the read is never
  /// older than local truth), re-read the room's rows, reconcile, and
  /// re-project views to local members. Join, joinGame and the reap path
  /// call it directly, and CatchUpRoom falls back to it when a room keeps
  /// moving; it materializes a room another instance created. Callers
  /// hold mu_.
  ///
  /// Returns whether the store answered the read — false is an outage, not
  /// an absent room, and the join paths label their refusal kUnavailable on
  /// it rather than blaming the client's state.
  bool RefreshRoomLocked(const std::string& room_id, Outbox& outbox, bool project_always);
  /// Returns whether local membership/games changed. When
  /// `project_always` is false, skips re-project on a no-op catch-up.
  bool ReconcileRoomLocked(const std::string& room_id, const HubStore::RoomRows& rows,
                           Outbox& outbox, bool project_always = true);
  /// Erases the game and its player mappings — for games the database
  /// says no longer exist. Only its watchers are told.
  void DropGameLocked(const GameRef& ref, Outbox& outbox);
  /// A table going without a final view: each watcher hears gameLeft and
  /// watches nothing. Callers hold mu_.
  void DropWatchersLocked(const std::string& game_id, GameEntry& entry, Outbox& outbox);
  void ListenRoomLocked(const std::string& room_id);
  void UnlistenRoomLocked(const std::string& room_id);

  /// Builders; callers hold mu_.
  moonbase::games::RoomState RoomStateLocked(const std::string& room_id, const Room& room) const;
  void StageRoomStateLocked(const std::string& room_id, Outbox& outbox) const;
  moonbase::games::GameView ViewLocked(const std::string& game_id, const GameEntry& entry,
                                       const std::string& viewer_id) const;
  moonbase::games::ChessView ChessViewLocked(const std::string& game_id,
                                             const GameEntry& entry) const;
  moonbase::games::CastleView CastleViewLocked(const std::string& game_id, const GameEntry& entry,
                                               const std::string& viewer_id) const;
  moonbase::games::RummyView RummyViewLocked(const std::string& game_id, const GameEntry& entry,
                                             const std::string& viewer_id) const;
  /// One viewer's gameJoined in the table's own vocabulary.
  moonbase::games::GameEvents JoinedEventLocked(const std::string& game_id, const GameEntry& entry,
                                                const std::string& viewer_id) const;
  void StageGameViewsLocked(const std::string& game_id, const GameEntry& entry,
                            Outbox& outbox) const;
  /// The game-over ceremony: final face-up views, then gameEnded, then
  /// the game is erased locally. Shared by the local finisher and the
  /// refresh path (a game another instance finished).
  void StageGameOverLocked(Room& room, const std::string& game_id, Outbox& outbox);
  /// Writes one domain event (game_events.h), if this deployment records
  /// them. `build` renders the line under the same instant the log files
  /// it by, so an event's timestamp and the hour it rolls into cannot
  /// disagree. Called under mu_, where every emit site already is.
  template <typename Build>
  void RecordLocked(Build&& build) {
    if (!event_writer_) return;
    const absl::Time now = absl::Now();
    event_writer_(now, build(now));
  }
  /// Adds a finish's stat deltas to the room's members; the same numbers
  /// rode the finish commit.
  void MirrorStatsLocked(Room& room, const std::vector<HubStore::StatsDelta>& deltas);
  /// The local finisher: mirrors the finish commit's stat deltas into the
  /// local member rows (after a kUnavailable leave, with no known commit)
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
  const GolfTestHooks hooks_;
  /// ADR-0020 reconnect grace, shared by the registry's per-seat timers
  /// and the boot reaper's one cohort deadline. Zero disables both.
  const std::chrono::seconds grace_period_;
  /// Rides every commit and rider as the notify payload, so an instance
  /// can tell its own wake-ups from the ones that carry news.
  const std::string instance_id_;
  std::mutex mu_;
  pg::Listener* listener_ = nullptr;  // owned by the caller; guarded by mu_
  EventWriter event_writer_;          // guarded by mu_; unset writes nothing
  std::unordered_map<std::string, Room> rooms_;
  /// Source of Room::revision stamps.
  uint64_t room_revisions_ = 0;
  /// The lobby's worlds, one per room and the plaza; guarded by mu_.
  World world_;
  /// The delivery class of each world update (opal #227). Under mu_.
  MoveCoalescing move_coalescing_;
  /// Each room's voice (#1590); guarded by mu_.
  Voice voice_;

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

  /// deja's tape (#1554). The client is set once before serving and read
  /// without a lock thereafter. tape_seq_ (the newest seq fanned out) and
  /// tape_priming_ (whether the next poll's events are dropped rather than
  /// shown) belong to whoever calls PollTapeOnce and to nobody else;
  /// tape_polling_ mirrors the gauge's level and moves under mu_, with the
  /// occupancy it answers.
  std::shared_ptr<deja::Client> tape_;
  std::int64_t tape_seq_ = 0;
  bool tape_priming_ = true;
  bool tape_polling_ = false;
  std::mutex tape_mu_;
  std::condition_variable tape_cv_;
  bool tape_stop_ = false;
  std::thread tape_poller_;

  /// The room heartbeat's stop flag and thread, apart from mu_ like the
  /// other background threads.
  std::mutex heartbeat_mu_;
  std::condition_variable heartbeat_cv_;
  bool heartbeat_stop_ = false;
  std::thread heartbeat_;

  /// Chess's opening and clock, set before serving.
  ChessOpener chess_opener_;
  /// Per (room, game): the epoch ms before which the sweep does not retry
  /// a flag the store refused as unavailable. Guarded by mu_.
  std::map<std::pair<std::string, std::string>, int64_t> chess_flag_retry_at_;
  std::function<absl::Time()> clock_;
  std::mutex chess_clock_mu_;
  std::condition_variable chess_clock_cv_;
  bool chess_clock_stop_ = false;
  std::thread chess_clocks_;

  /// Chess bots: the engine, set before serving; per (room, table) the
  /// game, counted by the table's finished games, whose engine failed and
  /// the epoch ms before which it is not asked again (guarded by mu_); and
  /// the bot thread.
  struct ChessBotRetry {
    std::size_t game;
    int64_t until;
  };
  ChessBotEngine chess_bot_engine_;
  ChessIndexer chess_indexer_;
  std::map<std::pair<std::string, std::string>, ChessBotRetry> chess_bot_retry_at_;
  std::mutex chess_bot_mu_;
  std::condition_variable chess_bot_cv_;
  bool chess_bot_stop_ = false;
  std::thread chess_bots_;

  /// Set before serving and read without a lock thereafter, like the
  /// tape's client; one per responder. Cleared first in ~GolfHub: their
  /// workers deliver through PumpChat, so they must be gone before
  /// anything that reaches.
  std::vector<std::unique_ptr<RoomBot>> bots_;

  // Declared last: destroyed first, joining registry threads before the
  // maps its on_expired callback touches go away. (The boot reaper is
  // joined earlier still, explicitly, in ~GolfHub.)
  Registry registry_;
};

}  // namespace games_hub

#endif
