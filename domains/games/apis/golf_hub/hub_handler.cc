#include "domains/games/apis/golf_hub/hub_handler.h"

#include <algorithm>
#include <deque>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "domains/games/libs/cards/card_mapper.h"
#include "domains/games/libs/cards/golf/player.h"

namespace golf_hub {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;
using moonbase::golf::GolfMove;
using moonbase::golf::GolfUpdate;

namespace {

constexpr std::size_t kMaxSeats = 4;
constexpr std::size_t kMaxChatLength = 500;

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

moonbase::golf::Card WireCard(const cards::Card& card) {
  moonbase::golf::Card wire;
  wire.rank = cards::CardMapper::rankToString(card.getRank());
  wire.suit = SuitString(card.getSuit());
  return wire;
}

std::string PhaseString(const golf::GameState& state) {
  if (state.isOver()) return "ended";
  if (state.revealCountdownActive()) return "peeking";
  if (state.getWhoKnocked() != -1) return "knocked";
  return "playing";
}

// Seats are integer indexes; the occupant's identity is the player id.
std::string PlayerIdAt(const golf::GameState& state, int seat) {
  return state.getPlayer(seat).getName().value_or("");
}

GolfEvents GolfUpdateEvent(GolfUpdate update) {
  moonbase::golf::GolfEvent event;
  event.update = std::move(update);
  return GolfEvents::FromGolf(std::move(event));
}

// Bounded rebase-retry for the conditional commits: each miss adopts the
// stored truth, so giving up leaves a consistent hub and a client who
// can simply resend.
constexpr int kMaxCommitAttempts = 3;

std::string InstanceId() {
  absl::BitGen gen;
  return absl::StrCat("hub-", absl::Hex(absl::Uniform<uint64_t>(gen), absl::kZeroPad16));
}

// The per-seat stat deltas a finished game applies — the payload of
// CommitGameFinish, and the same numbers FinalizeGameLocked mirrors into
// the local member rows.
std::vector<golf_hub::PgHubStore::StatsDelta> StatsDeltas(const golf::GameState& state) {
  const auto winner_indexes = state.winners();
  std::vector<golf_hub::PgHubStore::StatsDelta> deltas;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    golf_hub::PgHubStore::StatsDelta delta;
    delta.player_id = seat.getName().value_or("");
    delta.played = 1;
    delta.won = winner_indexes.contains(static_cast<int>(i)) ? 1 : 0;
    delta.score = seat.score();
    deltas.push_back(std::move(delta));
  }
  return deltas;
}

}  // namespace

HubHandler::HubHandler(std::shared_ptr<TicketVault> vault, std::shared_ptr<cards::Dealer> dealer,
                       std::shared_ptr<IdGenerator> ids, std::chrono::seconds grace_period,
                       std::shared_ptr<futility::otel::MetricsRecorder> metrics,
                       std::shared_ptr<PgHubStore> store)
    : vault_(std::move(vault)),
      dealer_(std::move(dealer)),
      ids_(std::move(ids)),
      metrics_(std::move(metrics)),
      store_(std::move(store)),
      instance_id_(InstanceId()),
      registry_([this, grace_period] {
        Registry::Options options;
        options.async_delivery = true;  // chains, not writer threads (ADR-0019)
        options.grace_period = grace_period;
        options.on_expired = [this](const std::string& id) { OnExpired(id); };
        return options;
      }()) {}

absl::Status HubHandler::RestoreFromStore() {
  if (store_ == nullptr) return absl::OkStatus();
  auto snapshot = store_->LoadSnapshot();
  if (!snapshot.ok()) return snapshot.status();
  const std::lock_guard<std::mutex> lock(mu_);
  for (const std::string& room_id : snapshot->rooms) rooms_[room_id];
  for (const PgHubStore::MemberRow& row : snapshot->members) {
    // Sockets did not survive the restart: everyone restores
    // disconnected and flips back on resume.
    Member member;
    member.connected = false;
    member.games_played = row.games_played;
    member.games_won = row.games_won;
    member.total_score = row.total_score;
    rooms_[row.room_id].members.emplace(row.player_id, member);
    player_room_[row.player_id] = row.room_id;
  }
  for (PgHubStore::GameRow& row : snapshot->games) {
    if (row.state.has_value() && row.state->isOver()) {
      // Terminal rows are durable handoffs for live instances that may
      // not have processed the finish wake yet. A restart ignores them
      // locally so ceremonies do not replay, but room deletion owns
      // their eventual cleanup through the foreign-key cascade.
      continue;
    }
    GameEntry entry;
    entry.roster = row.roster;
    entry.version = row.version;
    if (row.state.has_value()) entry.state.emplace(*std::move(row.state));
    for (const std::string& member_id : entry.roster) player_game_[member_id] = row.game_id;
    rooms_[row.room_id].games.emplace(row.game_id, std::move(entry));
  }
  LOG(INFO) << "hub restored " << snapshot->rooms.size() << " rooms, " << snapshot->members.size()
            << " members, " << snapshot->games.size() << " games";
  return absl::OkStatus();
}

void HubHandler::EnqueueWritesLocked(Writes& writes) {
  if (store_ != nullptr && !writes.empty()) store_->Enqueue(std::move(writes));
  writes.clear();
}

void HubHandler::StageLocked(Writes& writes, PgHubStore::Op op) const {
  if (store_ != nullptr) writes.push_back(std::move(op));
}

void HubHandler::StageMemberLocked(const std::string& room_id, const std::string& player_id,
                                   const Member& member, Writes& writes) const {
  if (store_ == nullptr) return;
  PgHubStore::MemberRow row;
  row.room_id = room_id;
  row.player_id = player_id;
  row.connected = member.connected;
  row.games_played = member.games_played;
  row.games_won = member.games_won;
  row.total_score = member.total_score;
  writes.push_back(PgHubStore::UpsertMember{std::move(row)});
}

void HubHandler::StageWakeLocked(const std::string& room_id, Writes& writes) const {
  StageLocked(writes, PgHubStore::Notify{RoomChannel(room_id), instance_id_});
}

HubHandler::Commit HubHandler::CommitEntryLocked(
    const std::string& room_id, const std::string& game_id, GameEntry& entry,
    const std::vector<std::string>& roster, const std::optional<golf::GameState>& state,
    const std::vector<PgHubStore::StatsDelta>* finish) {
  const int64_t version = entry.version + 1;
  if (store_ != nullptr) {
    // The async outbox may still hold rows this commit depends on — the
    // room behind the insert's FK, the membership behind the finish's
    // stat deltas. Drain it so the synchronous write never outruns the
    // queued ones. (The writer needs no lock we hold.)
    store_->Flush();
    PgHubStore::GameRow row;
    row.room_id = room_id;
    row.game_id = game_id;
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
      entry.roster = (*stored)->roster;
      entry.version = (*stored)->version;
      if ((*stored)->state.has_value()) entry.state.emplace(*std::move((*stored)->state));
      return Commit::kRebased;
    }
  }
  entry.roster = roster;
  if (state.has_value()) entry.state.emplace(*state);
  entry.version = version;
  return Commit::kCommitted;
}

void HubHandler::AttachListener(pg::Listener* listener) {
  const std::lock_guard<std::mutex> lock(mu_);
  listener_ = listener;
  if (listener_ == nullptr) return;
  for (const auto& [room_id, room] : rooms_) listener_->Listen(RoomChannel(room_id));
}

void HubHandler::OnNotify(const std::string& channel, const std::string& payload) {
  if (payload == instance_id_) return;  // our own commit; locals already heard it
  constexpr std::string_view kPrefix = "room_";
  if (!absl::StartsWith(channel, kPrefix)) return;
  const std::string room_id = channel.substr(kPrefix.size());
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // Only rooms we hold: a stale wake for a dropped room must not
    // resurrect it (join is the one path that materializes rooms).
    if (!rooms_.contains(room_id)) return;
    RefreshRoomLocked(room_id, outbox);
  }
  Deliver(outbox);
}

void HubHandler::RefreshRoomLocked(const std::string& room_id, Outbox& outbox) {
  if (store_ == nullptr) return;
  // The flush means this read can never be older than our own truth;
  // holding mu_ across it means nothing local moves in between. The
  // writer thread needs no lock we hold, so it drains freely.
  store_->Flush();
  auto rows = store_->LoadRoom(room_id);
  if (!rows.ok()) {
    // A missed wake is fine by protocol: the next read heals.
    LOG(WARNING) << "room " << room_id << " refresh failed: " << rows.status();
    return;
  }
  ReconcileRoomLocked(room_id, *rows, outbox);
}

void HubHandler::ReconcileRoomLocked(const std::string& room_id, const PgHubStore::RoomRows& rows,
                                     Outbox& outbox) {
  if (!rows.exists) {
    // Deleted by another instance; nothing local can outrank that.
    const auto room = rooms_.find(room_id);
    if (room == rooms_.end()) return;
    for (const auto& [member_id, member] : room->second.members) {
      if (auto it = player_room_.find(member_id);
          it != player_room_.end() && it->second == room_id) {
        player_room_.erase(it);
        player_game_.erase(member_id);
      }
    }
    rooms_.erase(room);
    UnlistenRoomLocked(room_id);
    return;
  }

  Room& room = rooms_[room_id];
  ListenRoomLocked(room_id);  // idempotent; covers a room just materialized

  // Members mirror the rows: every instance writes its own players'
  // rows, and the refresh flush made ours current.
  std::map<std::string, Member> members;
  for (const PgHubStore::MemberRow& row : rows.members) {
    Member member;
    member.connected = row.connected;
    member.games_played = row.games_played;
    member.games_won = row.games_won;
    member.total_score = row.total_score;
    members.emplace(row.player_id, member);
    player_room_[row.player_id] = room_id;
  }
  for (const auto& [member_id, member] : room.members) {
    if (members.contains(member_id)) continue;
    if (auto it = player_room_.find(member_id); it != player_room_.end() && it->second == room_id) {
      player_room_.erase(it);
      player_game_.erase(member_id);
    }
  }
  room.members = std::move(members);

  // Games: adopt rows that moved past us. A row that ended while we
  // hold the game is a remote finish — ceremony here, then it is gone
  // locally (the finisher owns the deferred row delete); an ended row
  // for a game we no longer hold is that delete still pending.
  std::set<std::string> stored_ids;
  for (const PgHubStore::GameRow& row : rows.games) {
    stored_ids.insert(row.game_id);
    const bool over = row.state.has_value() && row.state->isOver();
    const auto game = room.games.find(row.game_id);
    if (game == room.games.end()) {
      if (over) continue;
      GameEntry entry;
      entry.roster = row.roster;
      entry.version = row.version;
      if (row.state.has_value()) entry.state.emplace(*row.state);
      for (const std::string& member_id : entry.roster) player_game_[member_id] = row.game_id;
      room.games.emplace(row.game_id, std::move(entry));
      continue;
    }
    GameEntry& entry = game->second;
    if (row.version <= entry.version) continue;  // ours is current
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
    if (over) StageGameOverLocked(room_id, room, row.game_id, outbox);
  }
  for (auto game = room.games.begin(); game != room.games.end();) {
    if (stored_ids.contains(game->first)) {
      ++game;
      continue;
    }
    // Deleted remotely: a disbanded lobby or a finished game's cleanup.
    for (const std::string& member_id : game->second.roster) {
      if (auto it = player_game_.find(member_id);
          it != player_game_.end() && it->second == game->first) {
        player_game_.erase(it);
      }
    }
    game = room.games.erase(game);
  }

  // The wake contract: re-read, then re-project — local viewers get
  // current views whether or not anything above changed.
  for (const auto& [game_id, entry] : room.games) StageGameViewsLocked(game_id, entry, outbox);
  StageRoomStateLocked(room_id, outbox);
}

void HubHandler::DropGameLocked(const GameRef& ref) {
  for (const std::string& member_id : ref.entry->roster) {
    if (auto it = player_game_.find(member_id);
        it != player_game_.end() && it->second == ref.game_id) {
      player_game_.erase(it);
    }
  }
  ref.room->games.erase(ref.game_id);
}

void HubHandler::ListenRoomLocked(const std::string& room_id) {
  if (listener_ != nullptr) listener_->Listen(RoomChannel(room_id));
}

void HubHandler::UnlistenRoomLocked(const std::string& room_id) {
  if (listener_ != nullptr) listener_->Unlisten(RoomChannel(room_id));
}

smithy::Outcome<moonbase::golf::GetSessionOutput> HubHandler::GetSession(
    const moonbase::golf::GetSessionInput& input,
    const smithy::server::RequestContext& /*context*/) {
  std::string player_id;
  bool token_valid = false;
  if (input.resumeToken.has_value()) {
    if (auto resolved = vault_->ResolveResumeToken(*input.resumeToken)) {
      player_id = std::move(*resolved);
      token_valid = true;
    }
  }
  if (player_id.empty()) player_id = ids_->PlayerId();

  // A vault backed by a store can be down; a mint nothing recorded must
  // not reach the client. Unknown -> a non-leaking 500.
  absl::StatusOr<std::string> ticket = vault_->IssueTicket(player_id);
  if (!ticket.ok()) return smithy::Error::Unknown("credential store unavailable");

  moonbase::golf::GetSessionOutput output;
  output.playerId = player_id;
  output.ticket = *std::move(ticket);
  if (token_valid) {
    output.resumeToken = *input.resumeToken;
  } else {
    absl::StatusOr<std::string> resume = vault_->IssueResumeToken(player_id);
    if (!resume.ok()) return smithy::Error::Unknown("credential store unavailable");
    output.resumeToken = *std::move(resume);
  }
  return output;
}

smithy::eventstream::StreamTask HubHandler::Play(moonbase::golf::PlayInput input,
                                                 moonbase::golf::PlayAsyncServerStream& stream) {
  auto player = vault_->SpendTicket(input.ticket);
  if (!player.has_value()) {
    Count("stream_admissions_refused", {{"reason", "bad_ticket"}});
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  const std::string player_id = *player;

  // The blessed admission call (ADR-0022): pre-first-suspend, on the
  // launching handler thread, where its brief blocking is legal.
  const auto admission = registry_.ResumeOrAdd(
      player_id, [&stream] { return stream.Share(); }, std::chrono::seconds(1));
  if (admission == Registry::Admission::kRefused) {
    Count("stream_admissions_refused", {{"reason", "seat_conflict"}});
    co_return smithy::Error::Modeled("SeatConflict", "player already has a live connection");
  }
  Count("stream_sessions",
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
    const auto room_it = player_room_.find(player_id);
    if (room_it != player_room_.end()) room = room_it->second;
    // A resumed seat mid-game gets its current view back immediately.
    if (auto ref = FindGameLocked(player_id)) {
      moonbase::golf::GameJoined joined;
      joined.view = ViewLocked(ref->game_id, *ref->entry, player_id);
      resync.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
    }
  }
  const bool resumed = admission == Registry::Admission::kResumed || room.has_value();
  moonbase::golf::SessionReady ready;
  ready.playerId = player_id;
  ready.resumed = resumed;
  if (room.has_value()) ready.roomId = *room;
  Send(player_id, GolfEvents::FromSessionready(std::move(ready)));
  // A resumed seat's room sees the connected flip (and the resumer gets
  // the current snapshot it missed).
  if (room.has_value()) BroadcastRoom(*room);
  Deliver(resync);

  while (true) {
    auto received = co_await stream.Receive();
    if (!received.ok()) {
      // Abrupt loss (or our own slow-consumer close): park the seat for
      // the grace window (ADR-0020); expiry reaps it. Detach fails only
      // when the entry is already gone — nothing left to do then.
      if (registry_.Detach(player_id)) {
        TrackActive(-1);
        Count("stream_disconnects", {{"kind", "abrupt"}});
        SetConnected(player_id, false);
        if (auto current = CurrentRoom(player_id)) BroadcastRoom(*current);
      }
      co_return smithy::Unit{};
    }
    if (!received->has_value()) {
      // Clean close: a deliberate leave. Free the seat, game, and room.
      registry_.Remove(player_id);
      TrackActive(-1);
      Count("stream_disconnects", {{"kind", "clean"}});
      Outbox outbox;
      Writes writes;
      {
        const std::lock_guard<std::mutex> lock(mu_);
        LeaveEverywhere(player_id, outbox, writes);
        EnqueueWritesLocked(writes);
      }
      Deliver(outbox);
      co_return smithy::Unit{};
    }
    HandleCommand(player_id, **received);
  }
}

void HubHandler::HandleCommand(const std::string& player_id, const GolfCommands& command) {
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
        player_room_[player_id] = room_id;
        ListenRoomLocked(room_id);
        StageLocked(writes, PgHubStore::UpsertRoom{room_id});
        StageMemberLocked(room_id, player_id, member->second, writes);
        StageRoomStateLocked(room_id, outbox);
        EnqueueWritesLocked(writes);
      }
    }
    if (room_id.empty()) {
      Reject(player_id, "already in a room");
    } else {
      Deliver(outbox);
    }
    return;
  }

  if (const auto* join = command.as_joinRoom_or_null()) {
    Outbox outbox;
    Writes writes;
    bool joined = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!player_room_.contains(player_id)) {
        // The room may live on another instance (#1194 step 3): one
        // synchronous read materializes it here before refusing.
        if (!rooms_.contains(join->roomId)) RefreshRoomLocked(join->roomId, outbox);
        const auto room = rooms_.find(join->roomId);
        if (room != rooms_.end()) {
          const auto [member, inserted] = room->second.members.emplace(player_id, Member{});
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
    } else {
      Reject(player_id, "room unavailable or already in a room");
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
        moonbase::golf::RoomLeft ack;
        ack.roomId = it->second;
        outbox.To(player_id, GolfEvents::FromRoomleft(std::move(ack)));
        LeaveEverywhere(player_id, outbox, writes);
        EnqueueWritesLocked(writes);
        left = true;
      }
    }
    if (left) {
      Deliver(outbox);
    } else {
      Reject(player_id, "not in a room");
    }
    return;
  }

  if (command.as_getRoomState_or_null() != nullptr) {
    std::optional<GolfEvents> snapshot;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto it = player_room_.find(player_id);
      if (it != player_room_.end()) {
        const auto room = rooms_.find(it->second);
        if (room != rooms_.end()) {
          snapshot = GolfEvents::FromRoomstate(RoomStateLocked(it->second, room->second));
        }
      }
    }
    if (snapshot.has_value()) {
      Send(player_id, std::move(*snapshot));
    } else {
      Reject(player_id, "not in a room");
    }
    return;
  }

  if (const auto* chat = command.as_chat_or_null()) {
    if (chat->text.empty()) {
      Reject(player_id, "empty chat message");
      return;
    }
    if (chat->text.size() > kMaxChatLength) {
      Reject(player_id, "chat message too long");
      return;
    }
    Outbox outbox;
    bool in_room = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (Room* room = FindRoomLocked(player_id); room != nullptr) {
        in_room = true;
        moonbase::golf::ChatMessage message;
        message.playerId = player_id;
        message.text = chat->text;
        for (const auto& member : room->members) {
          outbox.To(member.first, GolfEvents::FromRoomchat(message));
        }
      }
    }
    if (in_room) {
      Deliver(outbox);
    } else {
      Reject(player_id, "not in a room");
    }
    return;
  }

  if (const auto* golf_command = command.as_golf_or_null()) {
    HandleMove(player_id, golf_command->move);
    return;
  }

  Reject(player_id, "unknown command");
}

void HubHandler::HandleMove(const std::string& player_id, const GolfMove& move) {
  if (move.as_createGame_or_null() != nullptr) {
    CreateGameMove(player_id);
    return;
  }
  if (const auto* join = move.as_joinGame_or_null()) {
    JoinGameMove(player_id, join->gameId);
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
      Reject(player_id, "not in a game");
    }
    return;
  }

  if (const auto* peek = move.as_peekCard_or_null()) {
    const auto position = golf::positionFromIndex(peek->cardIndex);
    if (!position.has_value()) {
      Reject(player_id, "invalid card index");
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
      Reject(player_id, "invalid card index");
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
      Reject(player_id, "invalid card index");
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

  Reject(player_id, "unknown move");
}

void HubHandler::CreateGameMove(const std::string& player_id) {
  Outbox outbox;
  std::string reason;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    Room* room = FindRoomLocked(player_id);
    if (room == nullptr) {
      reason = "not in a room";
    } else if (player_game_.contains(player_id)) {
      reason = "leave your current game first";
    } else {
      const std::string room_id = player_room_.at(player_id);
      for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
        std::string game_id = ids_->GameCode();
        while (room->games.contains(game_id)) game_id = ids_->GameCode();
        GameEntry& entry = room->games[game_id];
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
          reason = "storage unavailable; try again";
          break;
        }
        player_game_[player_id] = game_id;

        moonbase::golf::GameCreated announcement;
        announcement.gameId = game_id;
        announcement.createdBy = player_id;
        for (const auto& member : room->members) {
          outbox.To(member.first, GolfUpdateEvent(GolfUpdate::FromGamecreated(announcement)));
        }
        moonbase::golf::GameJoined joined;
        joined.view = ViewLocked(game_id, entry, player_id);
        outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
        StageRoomStateLocked(room_id, outbox);
        break;
      }
      if (reason.empty() && !player_game_.contains(player_id)) {
        reason = "could not allocate a game code; try again";
      }
    }
  }
  if (!reason.empty()) {
    Reject(player_id, std::move(reason));
  } else {
    Deliver(outbox);
  }
}

void HubHandler::JoinGameMove(const std::string& player_id, const std::string& game_id) {
  Outbox outbox;
  std::string reason;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    Room* room = FindRoomLocked(player_id);
    if (room == nullptr) {
      reason = "not in a room";
    } else if (player_game_.contains(player_id)) {
      reason = "leave your current game first";
    } else {
      const std::string room_id = player_room_.at(player_id);
      if (store_ != nullptr && !room->games.contains(game_id)) {
        // Another instance may have created it since our last wake.
        RefreshRoomLocked(room_id, outbox);
        room = FindRoomLocked(player_id);  // the refresh can drop us or the room
      }
      if (room == nullptr || !room->games.contains(game_id)) {
        reason = "game not found";
      } else {
        for (int attempt = 0; attempt < kMaxCommitAttempts; ++attempt) {
          GameEntry& entry = room->games.at(game_id);
          if (entry.started()) {
            reason = "game already started";
            break;
          }
          if (entry.roster.size() >= kMaxSeats) {
            reason = "game is full";
            break;
          }
          std::vector<std::string> roster = entry.roster;
          roster.push_back(player_id);
          const Commit commit =
              CommitEntryLocked(room_id, game_id, entry, roster, std::nullopt, nullptr);
          if (commit == Commit::kRebased) continue;
          if (commit == Commit::kGone) {
            room->games.erase(game_id);
            reason = "game not found";
            break;
          }
          if (commit == Commit::kUnavailable) {
            reason = "storage unavailable; try again";
            break;
          }
          player_game_[player_id] = game_id;

          moonbase::golf::GameJoined joined;
          joined.view = ViewLocked(game_id, entry, player_id);
          outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
          for (const std::string& recipient : entry.roster) {
            if (recipient == player_id) continue;
            moonbase::golf::GameStateUpdate update;
            update.view = ViewLocked(game_id, entry, recipient);
            outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
          }
          StageRoomStateLocked(room_id, outbox);
          break;
        }
        if (reason.empty() && !player_game_.contains(player_id)) {
          reason = "game changed; try again";
        }
      }
    }
  }
  if (!reason.empty()) {
    Reject(player_id, std::move(reason));
  } else {
    Deliver(outbox);
  }
}

void HubHandler::StartGameMove(const std::string& player_id) {
  Outbox outbox;
  std::string reason;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      reason = "not in a game";
    } else {
      bool started = false;
      for (int attempt = 0; attempt < kMaxCommitAttempts && reason.empty(); ++attempt) {
        if (ref->entry->started()) {
          reason = "game already started";
          break;
        }
        if (ref->entry->roster.size() < 2) {
          reason = "need at least 2 players to start";
          break;
        }
        std::deque<cards::Card> deck = dealer_->DealNewUnshuffledDeck();
        dealer_->ShuffleDeck(deck);
        auto dealt = golf::dealGolfGame(ref->game_id, ref->entry->roster, std::move(deck));
        if (!dealt.ok()) {
          reason = std::string(dealt.status().message());
          break;
        }
        const Commit commit = CommitEntryLocked(ref->room_id, ref->game_id, *ref->entry,
                                                ref->entry->roster, *std::move(dealt), nullptr);
        if (commit == Commit::kRebased) continue;  // the roster (or starter) raced us; redeal
        if (commit == Commit::kGone) {
          DropGameLocked(*ref);
          StageRoomStateLocked(ref->room_id, outbox);
          reason = "game no longer exists";
          break;
        }
        if (commit == Commit::kUnavailable) {
          reason = "storage unavailable; try again";
          break;
        }
        started = true;
        for (const std::string& recipient : ref->entry->roster) {
          outbox.To(recipient,
                    GolfUpdateEvent(GolfUpdate::FromGamestarted(moonbase::golf::GameStarted{})));
        }
        StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
        StageRoomStateLocked(ref->room_id, outbox);
        break;
      }
      if (reason.empty() && !started) reason = "game changed; try again";
    }
  }
  if (!reason.empty()) {
    Reject(player_id, std::move(reason));
  } else {
    Deliver(outbox);
  }
}

void HubHandler::EngineMove(const std::string& player_id, const MoveFn& move, MoveEffects effects) {
  Outbox outbox;
  Writes writes;
  std::string reason;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      reason = "not in a game";
    } else if (!ref->entry->started()) {
      reason = "game not started";
    } else {
      bool landed = false;
      // The issue's move loop: pure transition off the entry, then the
      // conditional commit; a miss adopts the stored truth and replays
      // the transition against it.
      for (int attempt = 0; attempt < kMaxCommitAttempts && reason.empty(); ++attempt) {
        const golf::GameState& state = *ref->entry->state;
        const int seat = state.playerIndex(player_id);
        if (seat < 0) {
          reason = "not seated in this game";
          break;
        }
        auto next = move(state, seat);
        if (!next.ok()) {
          reason = std::string(next.status().message());
          break;
        }
        const bool was_countdown = state.revealCountdownActive();
        // Compare occupant ids, not seat indexes — a mid-round leave
        // renumbers seats.
        const std::string previous_turn =
            effects.announce_turn ? PlayerIdAt(state, state.getWhoseTurn()) : std::string();
        const bool over = next->isOver();
        std::vector<PgHubStore::StatsDelta> deltas;
        if (over) deltas = StatsDeltas(*next);
        const Commit commit =
            CommitEntryLocked(ref->room_id, ref->game_id, *ref->entry, ref->entry->roster,
                              *std::move(next), over ? &deltas : nullptr);
        if (commit == Commit::kRebased) continue;  // another instance moved first
        if (commit == Commit::kGone) {
          DropGameLocked(*ref);
          StageRoomStateLocked(ref->room_id, outbox);
          reason = "game no longer exists";
          break;
        }
        if (commit == Commit::kUnavailable) {
          reason = "storage unavailable; try again";
          break;
        }
        landed = true;
        const golf::GameState& updated = *ref->entry->state;

        if (effects.announce_knock) {
          moonbase::golf::PlayerKnocked knocked;
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
            moonbase::golf::GameStateUpdate update;
            update.view = ViewLocked(ref->game_id, *ref->entry, player_id);
            outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
          } else {
            StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
          }
          if (effects.announce_turn) {
            const std::string current_turn = PlayerIdAt(updated, updated.getWhoseTurn());
            if (current_turn != previous_turn) {
              moonbase::golf::TurnChanged turn;
              turn.playerId = current_turn;
              for (const std::string& recipient : ref->entry->roster) {
                outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromTurnchanged(turn)));
              }
            }
          }
        }
        break;
      }
      if (reason.empty() && !landed) reason = "game changed; try again";
    }
    EnqueueWritesLocked(writes);
  }
  if (!reason.empty()) {
    Reject(player_id, std::move(reason));
  } else {
    Deliver(outbox);
  }
}

void HubHandler::SetConnected(const std::string& player_id, bool connected) {
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

std::optional<std::string> HubHandler::CurrentRoom(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return std::nullopt;
  return it->second;
}

HubHandler::Room* HubHandler::FindRoomLocked(const std::string& player_id) {
  const auto room_it = player_room_.find(player_id);
  if (room_it == player_room_.end()) return nullptr;
  const auto room = rooms_.find(room_it->second);
  return room != rooms_.end() ? &room->second : nullptr;
}

std::optional<HubHandler::GameRef> HubHandler::FindGameLocked(const std::string& player_id) {
  const auto room_it = player_room_.find(player_id);
  const auto game_it = player_game_.find(player_id);
  if (room_it == player_room_.end() || game_it == player_game_.end()) return std::nullopt;
  const auto room = rooms_.find(room_it->second);
  if (room == rooms_.end()) return std::nullopt;
  const auto game = room->second.games.find(game_it->second);
  if (game == room->second.games.end()) return std::nullopt;
  return GameRef{room_it->second, &room->second, game_it->second, &game->second};
}

void HubHandler::LeaveEverywhere(const std::string& player_id, Outbox& outbox, Writes& writes) {
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
    // One DeleteRoom; the row's cascade takes members and games with it.
    // The wake rider tells any instance that still holds the room (a
    // race, not the norm — an emptied room has no members anywhere).
    StageLocked(writes, PgHubStore::DeleteRoom{room_id});
    StageWakeLocked(room_id, writes);
    UnlistenRoomLocked(room_id);
    return;  // nobody left to tell
  }
  StageLocked(writes, PgHubStore::DeleteMember{room_id, player_id});
  StageWakeLocked(room_id, writes);
  StageRoomStateLocked(room_id, outbox);
}

void HubHandler::LeaveGameLocked(const std::string& player_id, Outbox& outbox, Writes& writes) {
  auto ref = FindGameLocked(player_id);
  player_game_.erase(player_id);
  if (!ref.has_value()) return;

  moonbase::golf::GameLeft ack;
  ack.gameId = ref->game_id;
  outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGameleft(std::move(ack))));

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
      StageLocked(writes, PgHubStore::DeleteGame{ref->room_id, ref->game_id});
      StageWakeLocked(ref->room_id, writes);
      StageRoomStateLocked(ref->room_id, outbox);
      return;
    }

    std::optional<golf::GameState> state;
    if (entry.started()) {
      const int seat = entry.state->playerIndex(player_id);
      if (seat >= 0) {
        auto next = entry.state->removePlayer(seat);
        state.emplace(next.ok() ? *std::move(next) : *entry.state);
      } else {
        state.emplace(*entry.state);
      }
    }
    const bool over = state.has_value() && state->isOver();
    std::vector<PgHubStore::StatsDelta> deltas;
    if (over) deltas = StatsDeltas(*state);
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
      StageRoomStateLocked(ref->room_id, outbox);
    }
    return;
  }
  // Three straight rebases: the entry now mirrors the store; the caller
  // (disconnect or a retried leave) comes around again.
  LOG(WARNING) << "game " << ref->room_id << "/" << ref->game_id
               << ": leave lost every commit race";
}

void HubHandler::BroadcastRoom(const std::string& room_id) {
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    StageRoomStateLocked(room_id, outbox);
  }
  Deliver(outbox);
}

void HubHandler::Reject(const std::string& player_id, std::string reason) {
  Count("stream_rejections", {{"reason", reason}});
  moonbase::golf::CommandRejected rejected;
  rejected.reason = std::move(reason);
  Send(player_id, GolfEvents::FromCommandrejected(std::move(rejected)));
}

void HubHandler::OnExpired(const std::string& player_id) {
  // Grace ran out (ADR-0020): the seat is gone; free the room and game
  // slots and tell whoever remains. Runs on the registry's expiry thread.
  Count("stream_seats_expired");
  Outbox outbox;
  Writes writes;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    // Cross-instance grace (#1194 step 3): the player may have resumed
    // on another instance while parked here. One fresh read decides —
    // a member row back at connected belongs to its new instance.
    bool resumed_elsewhere = false;
    if (store_ != nullptr) {
      if (const auto room_it = player_room_.find(player_id); room_it != player_room_.end()) {
        RefreshRoomLocked(room_it->second, outbox);
        if (Room* room = FindRoomLocked(player_id); room != nullptr) {
          const auto member = room->members.find(player_id);
          resumed_elsewhere = member != room->members.end() && member->second.connected;
        }
      }
    }
    if (!resumed_elsewhere) {
      LeaveEverywhere(player_id, outbox, writes);
      EnqueueWritesLocked(writes);
    }
  }
  Deliver(outbox);
}

void HubHandler::Deliver(Outbox& outbox) {
  for (auto& [player_id, event] : outbox.events) {
    Send(player_id, std::move(event));
  }
  outbox.events.clear();
}

void HubHandler::Count(const char* name, const std::map<std::string, std::string>& attributes) {
  if (metrics_) metrics_->RecordCounter(name, 1, attributes);
}

void HubHandler::TrackActive(int delta) {
  // Delta form, matching http_server_requests_active: the collector sums
  // an up-down counter into the live-session count.
  if (metrics_) metrics_->RecordGauge("stream_sessions_active", delta);
}

void HubHandler::CountCommand(const GolfCommands& command) {
  if (!metrics_) return;
  const auto* envelope = command.as_golf_or_null();
  const std::string name = envelope != nullptr ? absl::StrCat("golf.", envelope->move.case_name())
                                               : std::string(command.case_name());
  metrics_->RecordCounter("stream_commands", 1, {{"command", name}});
}

void HubHandler::Send(const std::string& player_id, GolfEvents event) {
  if (metrics_) {
    const auto* envelope = event.as_golf_or_null();
    const std::string name = envelope != nullptr
                                 ? absl::StrCat("golf.", envelope->update.case_name())
                                 : std::string(event.case_name());
    metrics_->RecordCounter("stream_events", 1, {{"event", name}});
  }
  registry_.SendTo(player_id, std::move(event));
}

moonbase::golf::RoomState HubHandler::RoomStateLocked(const std::string& room_id,
                                                      const Room& room) const {
  moonbase::golf::RoomState state;
  state.roomId = room_id;
  for (const auto& [member_id, member] : room.members) {
    moonbase::golf::PlayerInfo info;
    info.playerId = member_id;
    info.connected = member.connected;
    info.gamesPlayed = member.games_played;
    info.gamesWon = member.games_won;
    info.totalScore = member.total_score;
    state.players.push_back(std::move(info));
  }
  for (const auto& [game_id, entry] : room.games) {
    moonbase::golf::GameSummary summary;
    summary.gameId = game_id;
    summary.status = entry.started() ? PhaseString(*entry.state) : "waiting";
    summary.playerCount = static_cast<int>(entry.roster.size());
    state.games.push_back(std::move(summary));
  }
  return state;
}

void HubHandler::StageRoomStateLocked(const std::string& room_id, Outbox& outbox) const {
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return;
  const moonbase::golf::RoomState state = RoomStateLocked(room_id, room->second);
  for (const auto& member : room->second.members) {
    outbox.To(member.first, GolfEvents::FromRoomstate(state));
  }
}

moonbase::golf::GameView HubHandler::ViewLocked(const std::string& game_id, const GameEntry& entry,
                                                const std::string& viewer_id) const {
  moonbase::golf::GameView view;
  view.gameId = game_id;

  if (!entry.started()) {
    view.phase = "waiting";
    view.drawPileCount = 0;
    view.discardCount = 0;
    view.allPlayersPeeked = false;
    for (const std::string& roster_id : entry.roster) {
      moonbase::golf::GamePlayer player;
      player.playerId = roster_id;
      player.cards.resize(4);
      player.hasPeeked = false;
      view.players.push_back(std::move(player));
    }
    return view;
  }

  const golf::GameState& state = *entry.state;
  const bool ended = state.isOver();
  view.phase = PhaseString(state);
  if (!ended) view.currentPlayerId = PlayerIdAt(state, state.getWhoseTurn());
  view.drawPileCount = static_cast<int>(state.getDrawPile().size());
  view.discardCount = static_cast<int>(state.getDiscardPile().size());
  if (!state.getDiscardPile().empty()) view.discardTop = WireCard(state.getDiscardPile().back());
  if (state.getWhoKnocked() != -1) view.knockedPlayerId = PlayerIdAt(state, state.getWhoKnocked());
  view.allPlayersPeeked = state.allPlayersPeeked();
  // The drawn card rides only to the player who is looking at it.
  if (state.getPeekedAtDrawPile() && !ended &&
      PlayerIdAt(state, state.getWhoseTurn()) == viewer_id && !state.getDrawPile().empty()) {
    view.drawnCard = WireCard(state.getDrawPile().back());
  }

  for (const golf::Player& seat : state.getPlayers()) {
    const std::string occupant = seat.getName().value_or("");
    moonbase::golf::GamePlayer player;
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

void HubHandler::StageGameViewsLocked(const std::string& game_id, const GameEntry& entry,
                                      Outbox& outbox) const {
  for (const std::string& recipient : entry.roster) {
    moonbase::golf::GameStateUpdate update;
    update.view = ViewLocked(game_id, entry, recipient);
    outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
  }
}

void HubHandler::StageGameOverLocked(const std::string& room_id, Room& room,
                                     const std::string& game_id, Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.started()) return;
  const golf::GameState& state = *game->second.state;

  // Seat order, so the display string is stable.
  const auto winner_indexes = state.winners();
  std::vector<std::string> winner_ids;
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    if (winner_indexes.contains(static_cast<int>(i))) {
      winner_ids.push_back(state.getPlayer(static_cast<int>(i)).getName().value_or(""));
    }
  }

  moonbase::golf::GameEnded ended;
  ended.winner = absl::StrJoin(winner_ids, " & ");
  ended.winners = winner_ids;
  for (const golf::Player& seat : state.getPlayers()) {
    moonbase::golf::FinalScore score;
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

void HubHandler::FinalizeGameLocked(const std::string& room_id, Room& room,
                                    const std::string& game_id, Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.started()) return;
  const golf::GameState& state = *game->second.state;

  // Room-scoped running stats: every seat played, every winner won.
  // With a store these same deltas already rode the finish commit; this
  // mirrors them into the local rows (and IS the update in-memory).
  const auto winner_indexes = state.winners();
  for (std::size_t i = 0; i < state.getPlayers().size(); ++i) {
    const golf::Player& seat = state.getPlayer(static_cast<int>(i));
    const auto member = room.members.find(seat.getName().value_or(""));
    if (member == room.members.end()) continue;
    member->second.games_played++;
    member->second.total_score += seat.score();
    if (winner_indexes.contains(static_cast<int>(i))) member->second.games_won++;
  }

  StageGameOverLocked(room_id, room, game_id, outbox);
  // The terminal row is the durable handoff to other instances. It
  // remains until the room is deleted and its foreign-key cascade runs;
  // deleting it here can outrun a listener that has not handled the
  // finish wake yet.
  StageRoomStateLocked(room_id, outbox);
}

}  // namespace golf_hub
