#include "domains/games/apis/golf_hub/hub_handler.h"

#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

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

}  // namespace

HubHandler::HubHandler(std::shared_ptr<TicketVault> vault, std::shared_ptr<cards::Dealer> dealer,
                       std::shared_ptr<IdGenerator> ids, std::chrono::seconds grace_period,
                       std::shared_ptr<futility::otel::MetricsRecorder> metrics)
    : vault_(std::move(vault)),
      dealer_(std::move(dealer)),
      ids_(std::move(ids)),
      metrics_(std::move(metrics)),
      registry_([this, grace_period] {
        Registry::Options options;
        options.async_delivery = true;  // chains, not writer threads (ADR-0019)
        options.grace_period = grace_period;
        options.on_expired = [this](const std::string& id) { OnExpired(id); };
        return options;
      }()) {}

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

  moonbase::golf::GetSessionOutput output;
  output.playerId = player_id;
  output.ticket = vault_->IssueTicket(player_id);
  output.resumeToken = token_valid ? *input.resumeToken : vault_->IssueResumeToken(player_id);
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
  const bool resumed = admission == Registry::Admission::kResumed;
  Count("stream_sessions", {{"resumed", resumed ? "true" : "false"}});
  TrackActive(+1);

  std::optional<std::string> room;
  Outbox resync;
  if (resumed) {
    SetConnected(player_id, true);
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
      {
        const std::lock_guard<std::mutex> lock(mu_);
        LeaveEverywhere(player_id, outbox);
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
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!player_room_.contains(player_id)) {
        room_id = ids_->RoomId();
        rooms_[room_id].members.emplace(player_id, Member{});
        player_room_[player_id] = room_id;
        StageRoomStateLocked(room_id, outbox);
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
    bool joined = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room = rooms_.find(join->roomId);
      if (room != rooms_.end() && !player_room_.contains(player_id)) {
        room->second.members.emplace(player_id, Member{});
        player_room_[player_id] = join->roomId;
        StageRoomStateLocked(join->roomId, outbox);
        joined = true;
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
    bool left = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto it = player_room_.find(player_id);
      if (it != player_room_.end()) {
        moonbase::golf::RoomLeft ack;
        ack.roomId = it->second;
        outbox.To(player_id, GolfEvents::FromRoomleft(std::move(ack)));
        LeaveEverywhere(player_id, outbox);
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
    bool in_game = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      in_game = player_game_.contains(player_id);
      if (in_game) LeaveGameLocked(player_id, outbox);
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
      std::string game_id = ids_->GameCode();
      while (room->games.contains(game_id)) game_id = ids_->GameCode();
      GameEntry& entry = room->games[game_id];
      entry.roster.push_back(player_id);
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
      StageRoomStateLocked(player_room_.at(player_id), outbox);
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
      const auto game = room->games.find(game_id);
      if (game == room->games.end()) {
        reason = "game not found";
      } else if (game->second.started()) {
        reason = "game already started";
      } else if (game->second.roster.size() >= kMaxSeats) {
        reason = "game is full";
      } else {
        game->second.roster.push_back(player_id);
        player_game_[player_id] = game_id;

        moonbase::golf::GameJoined joined;
        joined.view = ViewLocked(game_id, game->second, player_id);
        outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
        for (const std::string& recipient : game->second.roster) {
          if (recipient == player_id) continue;
          moonbase::golf::GameStateUpdate update;
          update.view = ViewLocked(game_id, game->second, recipient);
          outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
        }
        StageRoomStateLocked(player_room_.at(player_id), outbox);
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
    } else if (ref->entry->started()) {
      reason = "game already started";
    } else if (ref->entry->roster.size() < 2) {
      reason = "need at least 2 players to start";
    } else {
      std::deque<cards::Card> deck = dealer_->DealNewUnshuffledDeck();
      dealer_->ShuffleDeck(deck);
      auto dealt = golf::dealGolfGame(ref->game_id, ref->entry->roster, std::move(deck));
      if (!dealt.ok()) {
        reason = std::string(dealt.status().message());
      } else {
        ref->entry->state.emplace(std::move(*dealt));
        for (const std::string& recipient : ref->entry->roster) {
          outbox.To(recipient,
                    GolfUpdateEvent(GolfUpdate::FromGamestarted(moonbase::golf::GameStarted{})));
        }
        StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
        StageRoomStateLocked(ref->room_id, outbox);
      }
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
  std::string reason;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    auto ref = FindGameLocked(player_id);
    if (!ref.has_value()) {
      reason = "not in a game";
    } else if (!ref->entry->started()) {
      reason = "game not started";
    } else {
      const golf::GameState& state = *ref->entry->state;
      const int seat = state.playerIndex(player_id);
      if (seat < 0) {
        reason = "not seated in this game";
      } else {
        auto next = move(state, seat);
        if (!next.ok()) {
          reason = std::string(next.status().message());
        } else {
          const bool was_countdown = state.revealCountdownActive();
          // Compare occupant ids, not seat indexes — a mid-round leave
          // renumbers seats.
          const std::string previous_turn =
              effects.announce_turn ? PlayerIdAt(state, state.getWhoseTurn()) : std::string();
          ref->entry->state.emplace(std::move(*next));
          const golf::GameState& updated = *ref->entry->state;

          if (effects.announce_knock) {
            moonbase::golf::PlayerKnocked knocked;
            knocked.playerId = player_id;
            for (const std::string& recipient : ref->entry->roster) {
              outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromPlayerknocked(knocked)));
            }
          }

          if (updated.isOver()) {
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

void HubHandler::SetConnected(const std::string& player_id, bool connected) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return;
  const auto room = rooms_.find(it->second);
  if (room == rooms_.end()) return;
  const auto member = room->second.members.find(player_id);
  if (member != room->second.members.end()) member->second.connected = connected;
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

void HubHandler::LeaveEverywhere(const std::string& player_id, Outbox& outbox) {
  LeaveGameLocked(player_id, outbox);

  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return;
  const std::string room_id = it->second;
  player_room_.erase(it);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return;
  room->second.members.erase(player_id);
  if (room->second.members.empty()) {
    rooms_.erase(room);
    return;  // nobody left to tell
  }
  StageRoomStateLocked(room_id, outbox);
}

void HubHandler::LeaveGameLocked(const std::string& player_id, Outbox& outbox) {
  auto ref = FindGameLocked(player_id);
  player_game_.erase(player_id);
  if (!ref.has_value()) return;

  moonbase::golf::GameLeft ack;
  ack.gameId = ref->game_id;
  outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGameleft(std::move(ack))));

  auto& roster = ref->entry->roster;
  roster.erase(std::remove(roster.begin(), roster.end(), player_id), roster.end());

  if (!ref->entry->started()) {
    if (roster.empty()) {
      ref->room->games.erase(ref->game_id);
    } else {
      StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
    }
    StageRoomStateLocked(ref->room_id, outbox);
    return;
  }

  const golf::GameState& state = *ref->entry->state;
  const int seat = state.playerIndex(player_id);
  if (seat >= 0) {
    auto next = state.removePlayer(seat);
    if (next.ok()) ref->entry->state.emplace(std::move(*next));
  }
  if (ref->entry->state->isOver()) {
    // Finalize stages the final views and the room's refreshed stats.
    FinalizeGameLocked(ref->room_id, *ref->room, ref->game_id, outbox);
  } else {
    StageGameViewsLocked(ref->game_id, *ref->entry, outbox);
    StageRoomStateLocked(ref->room_id, outbox);
  }
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
  {
    const std::lock_guard<std::mutex> lock(mu_);
    LeaveEverywhere(player_id, outbox);
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

void HubHandler::FinalizeGameLocked(const std::string& room_id, Room& room,
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

  // Room-scoped running stats: every seat played, every winner won.
  for (const golf::Player& seat : state.getPlayers()) {
    const std::string occupant = seat.getName().value_or("");
    const auto member = room.members.find(occupant);
    if (member == room.members.end()) continue;
    member->second.games_played++;
    member->second.total_score += seat.score();
    if (std::find(winner_ids.begin(), winner_ids.end(), occupant) != winner_ids.end()) {
      member->second.games_won++;
    }
  }

  // Final views (everything face up), then the result, then the room's
  // refreshed stats; the game itself is done and gone.
  StageGameViewsLocked(game_id, game->second, outbox);
  for (const std::string& recipient : game->second.roster) {
    outbox.To(recipient, GolfUpdateEvent(GolfUpdate::FromGameended(ended)));
    player_game_.erase(recipient);
  }
  room.games.erase(game);
  StageRoomStateLocked(room_id, outbox);
}

}  // namespace golf_hub
