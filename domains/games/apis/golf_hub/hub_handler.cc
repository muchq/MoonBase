#include "domains/games/apis/golf_hub/hub_handler.h"

#include <algorithm>
#include <deque>
#include <utility>
#include <vector>

#include "absl/strings/str_join.h"
#include "domains/games/libs/cards/golf/player.h"

namespace golf_hub {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;
using moonbase::golf::GolfMove;
using moonbase::golf::GolfUpdate;

namespace {

constexpr std::size_t kMaxSeats = 4;
constexpr std::size_t kMaxChatLength = 500;

// The v1 wire's card language, which the UI already renders.
std::string RankString(cards::Rank rank) {
  switch (rank) {
    case cards::Rank::Ace:
      return "A";
    case cards::Rank::Jack:
      return "J";
    case cards::Rank::Queen:
      return "Q";
    case cards::Rank::King:
      return "K";
    default:
      return std::to_string(static_cast<int>(rank) + 2);
  }
}

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
  wire.rank = RankString(card.getRank());
  wire.suit = SuitString(card.getSuit());
  return wire;
}

std::string PhaseString(const golf::GameState& state) {
  if (state.isOver()) return "ended";
  if (state.revealCountdownActive()) return "peeking";
  if (state.getWhoKnocked() != -1) return "knocked";
  return "playing";
}

std::string SeatName(const golf::GameState& state, int seat) {
  return state.getPlayer(seat).getName().value_or("");
}

GolfEvents GolfUpdateEvent(GolfUpdate update) {
  moonbase::golf::GolfEvent event;
  event.update = std::move(update);
  return GolfEvents::FromGolf(std::move(event));
}

}  // namespace

HubHandler::HubHandler(std::shared_ptr<TicketVault> vault, std::shared_ptr<cards::Dealer> dealer,
                       std::chrono::seconds grace_period)
    : vault_(std::move(vault)), dealer_(std::move(dealer)), registry_([this, grace_period] {
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
  if (player_id.empty()) player_id = WhimsicalId();

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
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  const std::string player_id = *player;

  // The blessed admission call (ADR-0022): pre-first-suspend, on the
  // launching handler thread, where its brief blocking is legal.
  const auto admission = registry_.ResumeOrAdd(
      player_id, [&stream] { return stream.Share(); }, std::chrono::seconds(1));
  if (admission == Registry::Admission::kRefused) {
    co_return smithy::Error::Modeled("SeatConflict", "player already has a live connection");
  }
  const bool resumed = admission == Registry::Admission::kResumed;

  std::optional<std::string> room;
  Outbox resync;
  if (resumed) {
    SetConnected(player_id, true);
    const std::lock_guard<std::mutex> lock(mu_);
    const auto room_it = player_room_.find(player_id);
    if (room_it != player_room_.end()) room = room_it->second;
    // A resumed seat mid-game gets its current view back immediately.
    const auto game_it = player_game_.find(player_id);
    if (room.has_value() && game_it != player_game_.end()) {
      const auto room_entry = rooms_.find(*room);
      if (room_entry != rooms_.end()) {
        const auto game = room_entry->second.games.find(game_it->second);
        if (game != room_entry->second.games.end()) {
          moonbase::golf::GameJoined joined;
          joined.view = ViewLocked(game->first, game->second, player_id);
          resync.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
        }
      }
    }
  }
  moonbase::golf::SessionReady ready;
  ready.playerId = player_id;
  ready.resumed = resumed;
  if (room.has_value()) ready.roomId = *room;
  registry_.SendTo(player_id, GolfEvents::FromSessionready(std::move(ready)));
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
        SetConnected(player_id, false);
        if (auto current = CurrentRoom(player_id)) BroadcastRoom(*current);
      }
      co_return smithy::Unit{};
    }
    if (!received->has_value()) {
      // Clean close: a deliberate leave. Free the seat, game, and room.
      registry_.Remove(player_id);
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
  if (command.as_createRoom_or_null() != nullptr) {
    std::string room_id;
    Outbox outbox;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!player_room_.contains(player_id)) {
        room_id = RandomId("r");
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
      registry_.SendTo(player_id, std::move(*snapshot));
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
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto it = player_room_.find(player_id);
      const auto room = it != player_room_.end() ? rooms_.find(it->second) : rooms_.end();
      if (room != rooms_.end()) {
        moonbase::golf::ChatMessage message;
        message.playerId = player_id;
        message.text = chat->text;
        for (const auto& member : room->second.members) {
          outbox.To(member.first, GolfEvents::FromRoomchat(message));
        }
      }
    }
    if (outbox.events.empty()) {
      Reject(player_id, "not in a room");
    } else {
      Deliver(outbox);
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
    Outbox outbox;
    std::string reason;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room_it = player_room_.find(player_id);
      const auto room = room_it != player_room_.end() ? rooms_.find(room_it->second) : rooms_.end();
      if (room == rooms_.end()) {
        reason = "not in a room";
      } else if (player_game_.contains(player_id)) {
        reason = "leave your current game first";
      } else {
        std::string game_id = GameCode();
        while (room->second.games.contains(game_id)) game_id = GameCode();
        GameEntry& entry = room->second.games[game_id];
        entry.roster.push_back(player_id);
        player_game_[player_id] = game_id;

        moonbase::golf::NewGameStarted announcement;
        announcement.gameId = game_id;
        for (const auto& member : room->second.members) {
          outbox.To(member.first, GolfUpdateEvent(GolfUpdate::FromNewgamestarted(announcement)));
        }
        moonbase::golf::GameJoined joined;
        joined.view = ViewLocked(game_id, entry, player_id);
        outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
        StageRoomStateLocked(room_it->second, outbox);
      }
    }
    if (!reason.empty()) {
      Reject(player_id, std::move(reason));
    } else {
      Deliver(outbox);
    }
    return;
  }

  if (const auto* join = move.as_joinGame_or_null()) {
    Outbox outbox;
    std::string reason;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room_it = player_room_.find(player_id);
      const auto room = room_it != player_room_.end() ? rooms_.find(room_it->second) : rooms_.end();
      if (room == rooms_.end()) {
        reason = "not in a room";
      } else if (player_game_.contains(player_id)) {
        reason = "leave your current game first";
      } else {
        const auto game = room->second.games.find(join->gameId);
        if (game == room->second.games.end()) {
          reason = "game not found";
        } else if (game->second.live()) {
          reason = "game already started";
        } else if (game->second.roster.size() >= kMaxSeats) {
          reason = "game is full";
        } else {
          game->second.roster.push_back(player_id);
          player_game_[player_id] = join->gameId;

          moonbase::golf::GameJoined joined;
          joined.view = ViewLocked(game->first, game->second, player_id);
          outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamejoined(std::move(joined))));
          for (const std::string& seat_id : game->second.roster) {
            if (seat_id == player_id) continue;
            moonbase::golf::GameStateUpdate update;
            update.view = ViewLocked(game->first, game->second, seat_id);
            outbox.To(seat_id, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
          }
          StageRoomStateLocked(room_it->second, outbox);
        }
      }
    }
    if (!reason.empty()) {
      Reject(player_id, std::move(reason));
    } else {
      Deliver(outbox);
    }
    return;
  }

  if (move.as_startGame_or_null() != nullptr) {
    Outbox outbox;
    std::string reason;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room_it = player_room_.find(player_id);
      const auto game_it = player_game_.find(player_id);
      const auto room = room_it != player_room_.end() ? rooms_.find(room_it->second) : rooms_.end();
      if (room == rooms_.end() || game_it == player_game_.end()) {
        reason = "not in a game";
      } else {
        const auto game = room->second.games.find(game_it->second);
        if (game == room->second.games.end()) {
          reason = "game not found";
        } else if (game->second.live()) {
          reason = "game already started";
        } else if (game->second.roster.size() < 2) {
          reason = "need at least 2 players to start";
        } else {
          // Deal like the Go hub: shuffled deck, four cards a seat, one
          // seeding the discard.
          std::deque<cards::Card> deck = dealer_->DealNewUnshuffledDeck();
          dealer_->ShuffleDeck(deck);
          std::vector<golf::Player> players;
          for (const std::string& seat_id : game->second.roster) {
            const cards::Card tl = deck.back();
            deck.pop_back();
            const cards::Card tr = deck.back();
            deck.pop_back();
            const cards::Card bl = deck.back();
            deck.pop_back();
            const cards::Card br = deck.back();
            deck.pop_back();
            players.emplace_back(seat_id, tl, tr, bl, br);
          }
          std::deque<cards::Card> discard{deck.back()};
          deck.pop_back();
          game->second.state.emplace(golf::GameState{std::move(deck),
                                               std::move(discard),
                                               std::move(players),
                                               false,
                                               0,
                                               -1,
                                               game->first,
                                               ""};

          for (const std::string& seat_id : game->second.roster) {
            outbox.To(seat_id,
                      GolfUpdateEvent(GolfUpdate::FromGamestarted(moonbase::golf::GameStarted{})));
          }
          StageGameViewsLocked(game->first, game->second, outbox);
          StageRoomStateLocked(room_it->second, outbox);
        }
      }
    }
    if (!reason.empty()) {
      Reject(player_id, std::move(reason));
    } else {
      Deliver(outbox);
    }
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

void HubHandler::EngineMove(const std::string& player_id, const MoveFn& move, MoveEffects effects) {
  Outbox outbox;
  std::string reason;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    const auto room_it = player_room_.find(player_id);
    const auto game_it = player_game_.find(player_id);
    Room* room = nullptr;
    if (room_it != player_room_.end()) {
      const auto found = rooms_.find(room_it->second);
      if (found != rooms_.end()) room = &found->second;
    }
    const auto game = room != nullptr && game_it != player_game_.end()
                          ? room->games.find(game_it->second)
                          : std::map<std::string, GameEntry>::iterator{};
    if (room == nullptr || game_it == player_game_.end() || game == room->games.end()) {
      reason = "not in a game";
    } else if (!game->second.live()) {
      reason = "game not started";
    } else {
      const golf::GameState& state = *game->second.state;
      const int seat = state.playerIndex(player_id);
      if (seat < 0) {
        reason = "not seated in this game";
      } else {
        auto next = move(state, seat);
        if (!next.ok()) {
          reason = std::string(next.status().message());
        } else {
          const bool was_countdown = state.revealCountdownActive();
          const std::string previous_turn = SeatName(state, state.getWhoseTurn());
          game->second.state.emplace(std::move(*next));
          const golf::GameState& updated = *game->second.state;

          if (effects.announce_knock) {
            moonbase::golf::PlayerKnocked knocked;
            knocked.playerId = player_id;
            for (const std::string& seat_id : game->second.roster) {
              outbox.To(seat_id, GolfUpdateEvent(GolfUpdate::FromPlayerknocked(knocked)));
            }
          }

          const bool countdown_started = !was_countdown && updated.revealCountdownActive();
          if (effects.peek_fanout && !countdown_started) {
            // A quiet peek: only the peeker's view changed.
            moonbase::golf::GameStateUpdate update;
            update.view = ViewLocked(game->first, game->second, player_id);
            outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
          } else {
            StageGameViewsLocked(game->first, game->second, outbox);
          }

          if (updated.isOver()) {
            FinalizeGameLocked(room_it->second, *room, game->first, outbox);
          } else if (effects.announce_turn) {
            const std::string current_turn = SeatName(updated, updated.getWhoseTurn());
            if (current_turn != previous_turn) {
              moonbase::golf::TurnChanged turn;
              turn.playerId = current_turn;
              for (const std::string& seat_id : game->second.roster) {
                outbox.To(seat_id, GolfUpdateEvent(GolfUpdate::FromTurnchanged(turn)));
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
  const auto game_it = player_game_.find(player_id);
  if (game_it == player_game_.end()) return;
  const std::string game_id = game_it->second;
  player_game_.erase(game_it);

  const auto room_it = player_room_.find(player_id);
  const auto room = room_it != player_room_.end() ? rooms_.find(room_it->second) : rooms_.end();
  if (room == rooms_.end()) return;
  const auto game = room->second.games.find(game_id);
  if (game == room->second.games.end()) return;

  moonbase::golf::GameLeft ack;
  ack.gameId = game_id;
  outbox.To(player_id, GolfUpdateEvent(GolfUpdate::FromGameleft(std::move(ack))));

  auto& roster = game->second.roster;
  roster.erase(std::remove(roster.begin(), roster.end(), player_id), roster.end());

  if (!game->second.live()) {
    if (roster.empty()) {
      room->second.games.erase(game);
    } else {
      for (const std::string& seat_id : roster) {
        moonbase::golf::GameStateUpdate update;
        update.view = ViewLocked(game_id, game->second, seat_id);
        outbox.To(seat_id, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
      }
    }
    StageRoomStateLocked(room_it->second, outbox);
    return;
  }

  const golf::GameState& state = *game->second.state;
  const int seat = state.playerIndex(player_id);
  if (seat >= 0) {
    auto next = state.removePlayer(seat);
    if (next.ok()) game->second.state.emplace(std::move(*next));
  }
  if (game->second.state->isOver()) {
    FinalizeGameLocked(room_it->second, room->second, game_id, outbox);
  } else {
    StageGameViewsLocked(game_id, game->second, outbox);
  }
  StageRoomStateLocked(room_it->second, outbox);
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
  moonbase::golf::CommandRejected rejected;
  rejected.reason = std::move(reason);
  registry_.SendTo(player_id, GolfEvents::FromCommandrejected(std::move(rejected)));
}

void HubHandler::OnExpired(const std::string& player_id) {
  // Grace ran out (ADR-0020): the seat is gone; free the room and game
  // slots and tell whoever remains. Runs on the registry's expiry thread.
  Outbox outbox;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    LeaveEverywhere(player_id, outbox);
  }
  Deliver(outbox);
}

void HubHandler::Deliver(Outbox& outbox) {
  for (auto& [player_id, event] : outbox.events) {
    registry_.SendTo(player_id, std::move(event));
  }
  outbox.events.clear();
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
    summary.status = entry.live() ? PhaseString(*entry.state) : "waiting";
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

  if (!entry.live()) {
    view.phase = "waiting";
    view.drawPileCount = 0;
    view.discardCount = 0;
    view.allPlayersPeeked = false;
    for (const std::string& seat_id : entry.roster) {
      moonbase::golf::GamePlayer player;
      player.playerId = seat_id;
      player.cards.resize(4);
      player.hasPeeked = false;
      view.players.push_back(std::move(player));
    }
    return view;
  }

  const golf::GameState& state = *entry.state;
  const bool ended = state.isOver();
  view.phase = PhaseString(state);
  if (!ended) view.currentPlayerId = SeatName(state, state.getWhoseTurn());
  view.drawPileCount = static_cast<int>(state.getDrawPile().size());
  view.discardCount = static_cast<int>(state.getDiscardPile().size());
  if (!state.getDiscardPile().empty()) view.discardTop = WireCard(state.getDiscardPile().back());
  if (state.getWhoKnocked() != -1) view.knockedPlayerId = SeatName(state, state.getWhoKnocked());
  view.allPlayersPeeked = state.allPlayersPeeked();
  // The drawn card rides only to the player who is looking at it.
  if (state.getPeekedAtDrawPile() && !ended && SeatName(state, state.getWhoseTurn()) == viewer_id &&
      !state.getDrawPile().empty()) {
    view.drawnCard = WireCard(state.getDrawPile().back());
  }

  for (const golf::Player& seat : state.getPlayers()) {
    const std::string seat_id = seat.getName().value_or("");
    moonbase::golf::GamePlayer player;
    player.playerId = seat_id;
    player.cards.resize(4);
    player.hasPeeked = seat.hasCompletedPeeks();
    if (ended) {
      const std::vector<cards::Card> hand = seat.allCards();
      for (int i = 0; i < 4; ++i) {
        player.cards[i].card = WireCard(hand[static_cast<std::size_t>(i)]);
        player.revealedIndexes.push_back(i);
      }
      player.score = seat.score();
    } else if (seat_id == viewer_id) {
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
  for (const std::string& seat_id : entry.roster) {
    moonbase::golf::GameStateUpdate update;
    update.view = ViewLocked(game_id, entry, seat_id);
    outbox.To(seat_id, GolfUpdateEvent(GolfUpdate::FromGamestate(std::move(update))));
  }
}

void HubHandler::FinalizeGameLocked(const std::string& room_id, Room& room,
                                    const std::string& game_id, Outbox& outbox) {
  const auto game = room.games.find(game_id);
  if (game == room.games.end() || !game->second.live()) return;
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
    const std::string seat_id = seat.getName().value_or("");
    const auto member = room.members.find(seat_id);
    if (member == room.members.end()) continue;
    member->second.games_played++;
    member->second.total_score += seat.score();
    if (std::find(winner_ids.begin(), winner_ids.end(), seat_id) != winner_ids.end()) {
      member->second.games_won++;
    }
  }

  // Final views (everything face up), then the result, then the room's
  // refreshed stats; the game itself is done and gone.
  StageGameViewsLocked(game_id, game->second, outbox);
  for (const std::string& seat_id : game->second.roster) {
    outbox.To(seat_id, GolfUpdateEvent(GolfUpdate::FromGameended(ended)));
    player_game_.erase(seat_id);
  }
  room.games.erase(game);
  StageRoomStateLocked(room_id, outbox);
}

}  // namespace golf_hub
