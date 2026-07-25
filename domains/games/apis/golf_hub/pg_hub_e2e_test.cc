// The step-2 restart-survival e2e (#1194): the same client flows as
// hub_e2e_test, but over durable credentials (step 1) and the rooms/games
// write-through — then the process "dies" (RestartHub) and a fresh hub
// over the same database has to seat everyone back into their live game.
// Real postgres via GOLF_HUB_TEST_DB_URL; skips otherwise.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "domains/games/apis/golf_hub/migrations.h"
#include "domains/games/apis/golf_hub/pg_hub_store.h"
#include "domains/games/apis/golf_hub/pg_ticket_vault.h"
#include "domains/games/apis/golf_hub/stream_test_fixture.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {
namespace {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;
using moonbase::golf::GolfMove;
using moonbase::golf::GolfUpdate;

// Receive helpers, as in hub_e2e_test: skip frames until the wanted case.
std::optional<GolfEvents> ReceiveCase(moonbase::golf::PlayClientStream& stream,
                                      const std::string& wanted) {
  for (int i = 0; i < 8; ++i) {
    auto received = stream.Receive();
    if (!received.ok() || !received->has_value()) return std::nullopt;
    if (wanted == (*received)->case_name()) return **received;
  }
  return std::nullopt;
}

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

class PgGolfHubFixture : public GolfHubStreamFixture {
 protected:
  void SetUp() override {
    url_ = std::getenv("GOLF_HUB_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') {
      GTEST_SKIP() << "GOLF_HUB_TEST_DB_URL unset";
    }
    auto db = pg::Client(url_);
    ASSERT_TRUE(RunMigrations(db).ok());
    ASSERT_TRUE(db.Exec("TRUNCATE rooms CASCADE").ok());
    ASSERT_TRUE(db.Exec("TRUNCATE tickets, resume_tokens").ok());
    GolfHubStreamFixture::SetUp();
  }

  std::shared_ptr<TicketVault> MakeVault() override {
    return std::make_shared<PgTicketVault>(std::make_shared<pg::Client>(url_),
                                           /*ticket_ttl=*/std::chrono::seconds(60),
                                           /*resume_ttl=*/std::chrono::seconds(60));
  }
  std::shared_ptr<PgHubStore> MakeStore() override {
    return std::make_shared<PgHubStore>(std::make_shared<pg::Client>(url_));
  }

  const char* url_ = nullptr;
};

namespace {

TEST_F(PgGolfHubFixture, LiveGameSurvivesARestart) {
  // Alice and bob: room, game, started, opening peeks done, one real
  // move played — a state with every kind of content.
  auto alice = OpenSeat();
  auto bob = OpenSeat();
  ASSERT_TRUE(alice.has_value() && bob.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveCase(bob->stream, "sessionReady").has_value());

  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  auto created = ReceiveCase(alice->stream, "roomState");
  ASSERT_TRUE(created.has_value());
  const std::string room_id = created->as_roomState_or_null()->roomId;
  moonbase::golf::JoinRoom join_room;
  join_room.roomId = room_id;
  ASSERT_TRUE(bob->stream.Send(GolfCommands::FromJoinroom(join_room)).ok());
  ASSERT_TRUE(ReceiveCase(bob->stream, "roomState").has_value());

  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto joined = ReceiveGolf(alice->stream, "gameJoined");
  ASSERT_TRUE(joined.has_value());
  const std::string game_id = joined->as_gameJoined_or_null()->view.gameId;
  moonbase::golf::JoinGame join_game;
  join_game.gameId = game_id;
  ASSERT_TRUE(bob->stream.Send(Move(GolfMove::FromJoingame(join_game))).ok());
  ASSERT_TRUE(ReceiveGolf(bob->stream, "gameJoined").has_value());
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromStartgame(moonbase::golf::StartGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice->stream, "gameStarted").has_value());
  ASSERT_TRUE(ReceiveGolf(bob->stream, "gameStarted").has_value());

  // Opening reveal for both, one hide, then alice draws and discards —
  // deck order and the discard pile now matter.
  for (auto* seat : {&*alice, &*bob}) {
    for (const int index : {0, 3}) {
      moonbase::golf::PeekCard peek;
      peek.cardIndex = index;
      ASSERT_TRUE(seat->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
    }
  }
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromHidecards(moonbase::golf::HideCards{}))).ok());
  ASSERT_TRUE(alice->stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      alice->stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  auto turn = ReceiveGolf(bob->stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob->player_id);
  // Drain alice to the same sync point: undelivered frames keep the
  // registry's delivery chain holding the stream, and the in-memory
  // pair's inline teardown would wait on it forever.
  ASSERT_TRUE(ReceiveGolf(alice->stream, "turnChanged").has_value());

  const std::string alice_token = alice->resume_token;
  const std::string bob_token = bob->resume_token;
  const std::string alice_id = alice->player_id;
  const std::string bob_id = bob->player_id;

  // The deploy: this process's hub dies, a fresh one boots from the
  // database. Resume tokens are rows (step 1), so the same identities
  // walk back in.
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  EXPECT_EQ(alice_back->player_id, alice_id);
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  EXPECT_EQ(*ready->as_sessionReady_or_null()->roomId, room_id);
  auto resynced = ReceiveGolf(alice_back->stream, "gameJoined");
  ASSERT_TRUE(resynced.has_value());
  const auto& view = resynced->as_gameJoined_or_null()->view;
  EXPECT_EQ(view.gameId, game_id);
  EXPECT_EQ(view.phase, "playing");
  EXPECT_EQ(view.currentPlayerId, bob_id);

  auto bob_back = OpenSeat(bob_token);
  ASSERT_TRUE(bob_back.has_value());
  EXPECT_EQ(bob_back->player_id, bob_id);
  ASSERT_TRUE(ReceiveCase(bob_back->stream, "sessionReady").has_value());
  ASSERT_TRUE(ReceiveGolf(bob_back->stream, "gameJoined").has_value());

  // The restored game is live, not a diorama: bob's turn carries on and
  // the move fans out to the restored alice.
  ASSERT_TRUE(bob_back->stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob_back->stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  auto next_turn = ReceiveGolf(alice_back->stream, "turnChanged");
  ASSERT_TRUE(next_turn.has_value());
  EXPECT_EQ(next_turn->as_turnChanged_or_null()->playerId, alice_id);
  // Same drain discipline for bob's copy of his own move.
  ASSERT_TRUE(ReceiveGolf(bob_back->stream, "turnChanged").has_value());
}

TEST_F(PgGolfHubFixture, StatsAndEmptyRoomsFollowTheTruth) {
  // A room whose members all leave must vanish from the database too.
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomLeft").has_value());

  store_->Flush();
  auto snapshot = store_->LoadSnapshot();
  ASSERT_TRUE(snapshot.ok()) << snapshot.status();
  EXPECT_TRUE(snapshot->rooms.empty());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());
}

}  // namespace
}  // namespace golf_hub
