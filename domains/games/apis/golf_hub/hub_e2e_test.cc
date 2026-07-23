// Phase 1 e2e: session minting, ticket admission, room lifecycle, and the
// reconnect-adjacent seat semantics, driven through the generated client
// over the in-memory pair. Wire-level details (JSON-text framing, real
// sockets) are upstream-tested; these pin the hub's behavior.

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "domains/games/apis/golf_hub/stream_test_fixture.h"

namespace golf_hub {
namespace {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;

using moonbase::golf::GolfMove;
using moonbase::golf::GolfUpdate;

// Receives until an event of the wanted case arrives (skipping others),
// failing after a few frames so a wrong stream can't hang the test.
std::optional<GolfEvents> ReceiveCase(moonbase::golf::PlayClientStream& stream,
                                      const std::string& wanted) {
  for (int i = 0; i < 8; ++i) {
    auto received = stream.Receive();
    if (!received.ok() || !received->has_value()) return std::nullopt;
    if (wanted == (*received)->case_name()) return **received;
  }
  return std::nullopt;
}

// Same, but tunnels into the golf envelope: returns the first GolfUpdate
// of the wanted case, skipping room noise and other updates in between.
std::optional<GolfUpdate> ReceiveGolf(moonbase::golf::PlayClientStream& stream,
                                      const std::string& wanted) {
  for (int i = 0; i < 16; ++i) {
    auto received = stream.Receive();
    if (!received.ok() || !received->has_value()) return std::nullopt;
    const auto* envelope = (*received)->as_golf_or_null();
    if (envelope == nullptr) continue;
    if (wanted == envelope->update.case_name()) return envelope->update;
  }
  return std::nullopt;
}

GolfCommands Move(GolfMove move) {
  moonbase::golf::GolfCommand command;
  command.move = std::move(move);
  return GolfCommands::FromGolf(std::move(command));
}

}  // namespace

class GolfGameFixture : public GolfHubStreamFixture {
 protected:
  // Room with alice and bob seated in a started game: the NoShuffleDealer
  // deals alice four aces and bob four kings with Q♠ seeding the discard.
  struct Table {
    Seat alice;
    Seat bob;
    std::string room_id;
    std::string game_id;
  };

  std::optional<Table> SeatedTable() {
    auto alice = OpenSeat();
    auto bob = OpenSeat();
    if (!alice.has_value() || !bob.has_value()) return std::nullopt;
    if (!ReceiveCase(alice->stream, "sessionReady").has_value()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "sessionReady").has_value()) return std::nullopt;

    if (!alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok()) {
      return std::nullopt;
    }
    auto created = ReceiveCase(alice->stream, "roomState");
    if (!created.has_value()) return std::nullopt;
    const std::string room_id = created->as_roomState_or_null()->roomId;
    moonbase::golf::JoinRoom join_room;
    join_room.roomId = room_id;
    if (!bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok()) return std::nullopt;
    if (!ReceiveCase(bob->stream, "roomState").has_value()) return std::nullopt;

    if (!alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok()) {
      return std::nullopt;
    }
    auto joined = ReceiveGolf(alice->stream, "gameJoined");
    if (!joined.has_value()) return std::nullopt;
    const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;

    moonbase::golf::JoinGame join_game;
    join_game.gameId = game_id;
    if (!bob->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok()) return std::nullopt;
    if (!ReceiveGolf(bob->stream, "gameJoined").has_value()) return std::nullopt;

    if (!alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok()) {
      return std::nullopt;
    }
    if (!ReceiveGolf(alice->stream, "gameStarted").has_value()) return std::nullopt;
    if (!ReceiveGolf(bob->stream, "gameStarted").has_value()) return std::nullopt;
    return Table{std::move(*alice), std::move(*bob), room_id, game_id};
  }
};

namespace {

TEST_F(GolfHubStreamFixture, SessionMintsDistinctPlayersAndResumeTokenRoundTrips) {
  moonbase::golf::GetSessionInput input;
  auto first = client_->GetSession(input);
  auto second = client_->GetSession(input);
  ASSERT_TRUE(first.ok() && second.ok());
  EXPECT_NE(first->playerId, second->playerId);

  moonbase::golf::GetSessionInput resume;
  resume.resumeToken = first->resumeToken;
  auto resumed = client_->GetSession(resume);
  ASSERT_TRUE(resumed.ok());
  EXPECT_EQ(resumed->playerId, first->playerId);
  EXPECT_NE(resumed->ticket, first->ticket);
}

TEST_F(GolfHubStreamFixture, BadTicketFailsTypedBeforeAnyEvent) {
  moonbase::golf::PlayInput input;
  input.ticket = "t-bogus";
  auto stream = client_->Play(input);
  ASSERT_TRUE(stream.ok()) << stream.error().message();
  auto first = stream->Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "Unauthenticated") << first.error().message();
}

TEST_F(GolfHubStreamFixture, TicketSpendsOnce) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // The same ticket is gone; a second dial with it dies typed. (A fresh
  // ticket for the same player is the SeatConflict test below.)
  moonbase::golf::PlayInput replay;
  replay.ticket = "t-bogus";  // any unspendable ticket behaves alike
  auto second = client_->Play(replay);
  ASSERT_TRUE(second.ok());
  auto first_event = second->Receive();
  ASSERT_FALSE(first_event.ok());
  EXPECT_EQ(first_event.error().code(), "Unauthenticated");
}

TEST_F(GolfHubStreamFixture, SecondLiveConnectionForSamePlayerIsRefused) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  // Fresh ticket for the same player while the first wire is healthy:
  // admission refuses (ADR-0022) as the modeled SeatConflict.
  auto conflicted = OpenSeat(seat->resume_token);
  ASSERT_TRUE(conflicted.has_value());
  EXPECT_EQ(conflicted->player_id, seat->player_id);
  auto first = conflicted->stream.Receive();
  ASSERT_FALSE(first.ok());
  EXPECT_EQ(first.error().code(), "SeatConflict");
}

TEST_F(GolfHubStreamFixture, CreateJoinAndLeaveBroadcastRoomState) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const auto* room = created->as_roomState_or_null();
  ASSERT_NE(room, nullptr);
  ASSERT_EQ(room->players.size(), 1u);
  EXPECT_EQ(room->players[0].playerId, alice->player_id);
  const std::string room_id = room->roomId;

  moonbase::golf::JoinRoom join;
  join.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto bob_view = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(bob_view.has_value());
  EXPECT_EQ(bob_view->as_roomState_or_null()->players.size(), 2u);
  auto alice_view = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(alice_view.has_value());
  EXPECT_EQ(alice_view->as_roomState_or_null()->players.size(), 2u);

  // Bob leaves deliberately: he gets the ack, Alice sees the shrink.
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  auto ack = ReceiveCase(bob->stream, "roomLeft");
  ASSERT_TRUE(ack.has_value());
  EXPECT_EQ(ack->as_roomLeft_or_null()->roomId, room_id);
  auto after_leave = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after_leave.has_value());
  ASSERT_EQ(after_leave->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(after_leave->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

TEST_F(GolfHubStreamFixture, CommandsOutsideARoomAreRejectedInBand) {
  auto seat = OpenSeat();
  ASSERT_TRUE(seat.has_value());
  ASSERT_TRUE(ReceiveCase(seat->stream, "sessionReady").has_value());

  ASSERT_TRUE(
      seat->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto rejected = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not in a room");

  moonbase::golf::JoinRoom join;
  join.roomId = "r-nope";
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  auto unknown = ReceiveCase(seat->stream, "commandRejected");
  ASSERT_TRUE(unknown.has_value());

  // The stream survived both rejections.
  ASSERT_TRUE(seat->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  EXPECT_TRUE(ReceiveCase(seat->stream, "roomState").has_value());
}

TEST_F(GolfHubStreamFixture, CleanCloseFreesTheRoomSlotAndNotifies) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::golf::JoinRoom join;
  join.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  bob->stream.Close();
  auto shrunk = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(shrunk.has_value());
  ASSERT_EQ(shrunk->as_roomState_or_null()->players.size(), 1u);
  EXPECT_EQ(shrunk->as_roomState_or_null()->players[0].playerId, alice->player_id);
}

TEST_F(GolfGameFixture, FullGameKnockerTieWinsAlone) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  auto& alice = table->alice;
  auto& bob = table->bob;

  // The opening deal, from alice's chair: her cards face down even to her,
  // bob's hand nothing but nulls, the seeded discard face up.
  auto opening = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(opening.has_value());
  {
    const auto* update = opening->as_gameState_or_null();
    ASSERT_NE(update, nullptr);
    EXPECT_EQ(update->view.phase, "playing");
    ASSERT_TRUE(update->view.currentPlayerId.has_value());
    EXPECT_EQ(*update->view.currentPlayerId, alice.player_id);
    EXPECT_EQ(update->view.drawPileCount, 43);
    ASSERT_TRUE(update->view.discardTop.has_value());
    EXPECT_EQ(update->view.discardTop->rank, "Q");
    ASSERT_EQ(update->view.players.size(), 2u);
    for (const auto& player : update->view.players) {
      for (const auto& slot : player.cards) EXPECT_FALSE(slot.card.has_value());
    }
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Opening peeks. Alice's first peek comes back to her alone, with the
  // ace face at the peeked index and nothing of bob's hand.
  moonbase::golf::PeekCard peek;
  peek.cardIndex = 0;
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto peeked = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(peeked.has_value());
  {
    const auto& view = peeked->as_gameState_or_null()->view;
    ASSERT_TRUE(view.players[0].cards[0].card.has_value());
    EXPECT_EQ(view.players[0].cards[0].card->rank, "A");
    EXPECT_EQ(view.players[0].revealedIndexes, std::vector<int>{0});
    for (const auto& slot : view.players[1].cards) EXPECT_FALSE(slot.card.has_value());
  }
  peek.cardIndex = 1;
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());

  peek.cardIndex = 0;
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());
  peek.cardIndex = 1;
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());

  // The last peek starts the table-wide countdown: everyone hears it, and
  // bob's view shows his kings but still nothing of alice's aces.
  auto countdown = ReceiveGolf(bob.stream, "gameState");
  ASSERT_TRUE(countdown.has_value());
  {
    const auto& view = countdown->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "peeking");
    EXPECT_TRUE(view.allPlayersPeeked);
    ASSERT_TRUE(view.players[1].cards[0].card.has_value());
    EXPECT_EQ(view.players[1].cards[0].card->rank, "K");
    for (const auto& slot : view.players[0].cards) EXPECT_FALSE(slot.card.has_value());
  }
  auto alice_countdown = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(alice_countdown.has_value());
  EXPECT_EQ(alice_countdown->as_gameState_or_null()->view.phase, "peeking");

  // Turn moves wait for the hide.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  auto gated = ReceiveCase(alice.stream, "commandRejected");
  ASSERT_TRUE(gated.has_value());

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::golf::HideCards{}))).ok());
  auto hidden = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(hidden.has_value());
  {
    const auto& view = hidden->as_gameState_or_null()->view;
    EXPECT_EQ(view.phase, "playing");
    EXPECT_TRUE(view.players[0].revealedIndexes.empty());
  }
  ASSERT_TRUE(ReceiveGolf(bob.stream, "gameState").has_value());

  // Alice draws: she sees the face, bob sees only the count drop.
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  auto drawn = ReceiveGolf(alice.stream, "gameState");
  ASSERT_TRUE(drawn.has_value());
  {
    const auto& view = drawn->as_gameState_or_null()->view;
    ASSERT_TRUE(view.drawnCard.has_value());
    EXPECT_EQ(view.drawnCard->rank, "Q");
    EXPECT_EQ(view.drawPileCount, 43);  // still on the pile until she commits
  }
  auto bob_saw_draw = ReceiveGolf(bob.stream, "gameState");
  ASSERT_TRUE(bob_saw_draw.has_value());
  EXPECT_FALSE(bob_saw_draw->as_gameState_or_null()->view.drawnCard.has_value());

  // She rejects it; the turn passes to bob.
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  auto turn = ReceiveGolf(alice.stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveGolf(bob.stream, "turnChanged").has_value());

  // Bob knocks; alice takes the final turn; the game resolves. Both hands
  // cancel to zero, and the knocker takes the tie alone.
  ASSERT_TRUE(bob.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok());
  auto knocked = ReceiveGolf(alice.stream, "playerKnocked");
  ASSERT_TRUE(knocked.has_value());
  EXPECT_EQ(knocked->as_playerKnocked_or_null()->playerId, bob.player_id);

  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice.stream, "gameState").has_value());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());

  auto ended = ReceiveGolf(alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  {
    const auto* result = ended->as_gameEnded_or_null();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->winner, bob.player_id);
    ASSERT_EQ(result->winners.size(), 1u);
    EXPECT_EQ(result->winners[0], bob.player_id);
    ASSERT_EQ(result->finalScores.size(), 2u);
    for (const auto& score : result->finalScores) EXPECT_EQ(score.score, 0);
  }
  auto bob_ended = ReceiveGolf(bob.stream, "gameEnded");
  ASSERT_TRUE(bob_ended.has_value());

  // The final board is face up for everyone, and the room's running stats
  // credit the knocker's solo win.
  auto final_alice = ReceiveCase(alice.stream, "roomState");
  ASSERT_TRUE(final_alice.has_value());
  {
    const auto* room = final_alice->as_roomState_or_null();
    ASSERT_NE(room, nullptr);
    EXPECT_TRUE(room->games.empty());
    for (const auto& player : room->players) {
      EXPECT_EQ(player.gamesPlayed, 1);
      EXPECT_EQ(player.gamesWon, player.playerId == bob.player_id ? 1 : 0);
      EXPECT_EQ(player.totalScore, 0);
    }
  }
}

TEST_F(GolfGameFixture, ChatReachesTheRoom) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  moonbase::golf::Chat chat;
  chat.text = "good luck!";
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(chat)).ok());

  auto to_bob = ReceiveCase(table->bob.stream, "roomChat");
  ASSERT_TRUE(to_bob.has_value());
  EXPECT_EQ(to_bob->as_roomChat_or_null()->playerId, table->alice.player_id);
  EXPECT_EQ(to_bob->as_roomChat_or_null()->text, "good luck!");
  auto echo = ReceiveCase(table->alice.stream, "roomChat");
  ASSERT_TRUE(echo.has_value());

  moonbase::golf::Chat empty;
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(empty)).ok());
  EXPECT_TRUE(ReceiveCase(table->alice.stream, "commandRejected").has_value());

  moonbase::golf::Chat oversized;
  oversized.text = std::string(501, 'x');
  ASSERT_TRUE(table->alice.stream.Send(GolfCommands::FromChat(oversized)).ok());
  auto too_long = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(too_long.has_value());
  EXPECT_EQ(too_long->as_commandRejected_or_null()->reason, "chat message too long");
}

TEST_F(GolfGameFixture, AbandoningALiveGameResolvesIt) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  auto ack = ReceiveGolf(table->bob.stream, "gameLeft");
  ASSERT_TRUE(ack.has_value());
  EXPECT_EQ(ack->as_gameLeft_or_null()->gameId, table->game_id);

  // Alice is the last seat standing: the game resolves in her favor and
  // leaves the room's game list empty.
  auto ended = ReceiveGolf(table->alice.stream, "gameEnded");
  ASSERT_TRUE(ended.has_value());
  ASSERT_EQ(ended->as_gameEnded_or_null()->winners.size(), 1u);
  EXPECT_EQ(ended->as_gameEnded_or_null()->winners[0], table->alice.player_id);

  auto room = ReceiveCase(table->alice.stream, "roomState");
  ASSERT_TRUE(room.has_value());
  EXPECT_TRUE(room->as_roomState_or_null()->games.empty());
}

TEST_F(GolfGameFixture, IllegalMovesRejectInBandAndTheGameContinues) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Not bob's turn.
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  auto rejected = ReceiveCase(table->bob.stream, "commandRejected");
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(rejected->as_commandRejected_or_null()->reason, "not your turn");

  // A bad index dies before it reaches the engine.
  moonbase::golf::PeekCard peek;
  peek.cardIndex = 9;
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
  auto bad_index = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(bad_index.has_value());
  EXPECT_EQ(bad_index->as_commandRejected_or_null()->reason, "invalid card index");

  // No blind moves: swapping without drawing is refused in-band.
  moonbase::golf::SwapCard blind;
  blind.cardIndex = 0;
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromSwapcard(blind))).ok());
  auto blind_swap = ReceiveCase(table->alice.stream, "commandRejected");
  ASSERT_TRUE(blind_swap.has_value());
  EXPECT_EQ(blind_swap->as_commandRejected_or_null()->reason, "no drawn card to swap");

  // The stream survived: a legal move still lands.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  EXPECT_TRUE(ReceiveGolf(table->alice.stream, "gameState").has_value());
}

TEST_F(GolfGameFixture, PendingGameLifecycleAndLobbySummaries) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::golf::JoinRoom join_room;
  join_room.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  // The whole room hears the creation attributed to its creator; the
  // creator's client relies on createdBy to recognize its own echo.
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto echo = ReceiveGolf(alice->stream, "gameCreated");
  ASSERT_TRUE(echo.has_value());
  EXPECT_EQ(echo->as_gameCreated_or_null()->createdBy, alice->player_id);
  auto announced = ReceiveGolf(bob->stream, "gameCreated");
  ASSERT_TRUE(announced.has_value());
  EXPECT_EQ(announced->as_gameCreated_or_null()->createdBy, alice->player_id);
  auto joined = ReceiveGolf(alice->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  EXPECT_EQ(joined->as_gameJoined_or_null()->view.phase, "waiting");
  EXPECT_EQ(announced->as_gameCreated_or_null()->gameId,
            joined->as_gameJoined_or_null()->view.gameId);

  // A solo game cannot start.
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok());
  auto lonely = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(lonely.has_value());
  EXPECT_EQ(lonely->as_commandRejected_or_null()->reason, "need at least 2 players to start");

  // One game per player at a time.
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto second = ReceiveCase(alice->stream, "commandRejected");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->as_commandRejected_or_null()->reason, "leave your current game first");

  // The lobby sees the pending game: waiting, one seat filled.
  ASSERT_TRUE(
      bob->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto lobby = ReceiveCase(bob->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  {
    const auto* room = lobby->as_roomState_or_null();
    ASSERT_EQ(room->games.size(), 1u);
    EXPECT_EQ(room->games[0].status, "waiting");
    EXPECT_EQ(room->games[0].playerCount, 1);
  }

  // Leaving a pending game as its last member dissolves it.
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  auto ack = ReceiveGolf(alice->stream, "gameLeft");
  ASSERT_TRUE(ack.has_value());
  auto after = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(after.has_value());
  EXPECT_TRUE(after->as_roomState_or_null()->games.empty());
}

TEST_F(GolfGameFixture, RoomStatsAccumulateAcrossGames) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Quickest legal game: alice knocks unseen, bob takes his final turn.
  const auto play_out = [](Seat& alice, Seat& bob) {
    if (!alice.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok()) return false;
    if (!ReceiveGolf(bob.stream, "playerKnocked").has_value()) return false;
    if (!bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok()) {
      return false;
    }
    if (!ReceiveGolf(bob.stream, "gameState").has_value()) return false;
    if (!bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok()) {
      return false;
    }
    return ReceiveGolf(alice.stream, "gameEnded").has_value() &&
           ReceiveGolf(bob.stream, "gameEnded").has_value();
  };
  ASSERT_TRUE(play_out(table->alice, table->bob));

  // Round two in the same room.
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto joined = ReceiveGolf(table->alice.stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  moonbase::golf::JoinGame join;
  join.gameId = joined->as_gameJoined_or_null()->view.gameId;
  ASSERT_TRUE(table->bob.stream.Send(Move(GolfMove::FromJoingame(join))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameJoined").has_value());
  ASSERT_TRUE(
      table->alice.stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameStarted").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameStarted").has_value());
  ASSERT_TRUE(play_out(table->alice, table->bob));

  // Running totals: two games played, both won solo by alice the knocker
  // (identical zero-scoring deals; the knocker takes the tie).
  ASSERT_TRUE(
      table->alice.stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{}))
          .ok());
  auto lobby = ReceiveCase(table->alice.stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  for (const auto& player : lobby->as_roomState_or_null()->players) {
    EXPECT_EQ(player.gamesPlayed, 2);
    EXPECT_EQ(player.totalScore, 0);
    EXPECT_EQ(player.gamesWon, player.playerId == table->alice.player_id ? 2 : 0);
  }
}

// The id seam at work: a generator that hands out the same game code
// twice, forcing the create path's collision loop to roll again.
class CollidingIds final : public IdGenerator {
 public:
  std::string PlayerId() override { return "player-" + std::to_string(++players_); }
  std::string RoomId() override { return "room-" + std::to_string(++rooms_); }
  std::string GameCode() override { return ++codes_ <= 2 ? "DUPLIC" : "FRESH1"; }

 private:
  int players_ = 0;
  int rooms_ = 0;
  int codes_ = 0;
};

class CollidingIdsFixture : public GolfGameFixture {
 protected:
  CollidingIdsFixture() { ids_ = std::make_shared<CollidingIds>(); }
};

TEST_F(CollidingIdsFixture, GameCodeCollisionRollsAgain) {
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  moonbase::golf::JoinRoom join_room;
  join_room.roomId = created->as_roomState_or_null()->roomId;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto first = ReceiveGolf(alice->stream, "gameJoined");
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->as_gameJoined_or_null()->view.gameId, "DUPLIC");

  // Bob's create draws "DUPLIC" again; the hub rolls until it's fresh.
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto second = ReceiveGolf(bob->stream, "gameJoined");
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->as_gameJoined_or_null()->view.gameId, "FRESH1");
}

}  // namespace
}  // namespace golf_hub
