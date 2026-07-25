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
#include <vector>

#include "domains/games/apis/golf_hub/migrations.h"
#include "domains/games/apis/golf_hub/pg_hub_store.h"
#include "domains/games/apis/golf_hub/pg_ticket_vault.h"
#include "domains/games/apis/golf_hub/stream_test_fixture.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfMove;

class PgGolfHubFixture : public GolfHubStreamFixture {
 protected:
  void SetUp() override {
    url_ = std::getenv("GOLF_HUB_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') {
      GTEST_SKIP() << "GOLF_HUB_TEST_DB_URL unset";
    }
    pg::Client db(url_);
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

  // Simulates the process dying and a fresh instance booting over the
  // same database. A crash closes nothing and says no goodbyes, so the
  // old generation is parked as-is; only the store flushes, because the
  // row truth must be complete before the successor reads it. The
  // retired hub is NOT inert: TearDown's Close() runs its clean-close
  // path and its store then writes the goodbyes a real crash never
  // would — every DB assertion must come before TearDown.
  void RestartHub() {
    if (store_ != nullptr) store_->Flush();
    retired_.push_back(
        {std::move(server_), std::move(client_), std::move(handler_), std::move(store_)});
    BuildHub();
  }

  // A store-side view of the rows, flushed first so staged writes are in.
  PgHubStore::Snapshot Rows() {
    store_->Flush();
    auto snapshot = store_->LoadSnapshot();
    EXPECT_TRUE(snapshot.ok()) << snapshot.status();
    return snapshot.value_or(PgHubStore::Snapshot{});
  }

  const char* url_ = nullptr;

  struct Generation {
    std::unique_ptr<moonbase::golf::GolfHubServer> server;
    std::unique_ptr<moonbase::golf::GolfHubClient> client;
    std::shared_ptr<HubHandler> handler;
    std::shared_ptr<PgHubStore> store;
  };
  std::vector<Generation> retired_;
};

namespace {

TEST_F(PgGolfHubFixture, LiveGameSurvivesARestart) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  Seat& alice = table->alice;
  Seat& bob = table->bob;

  // Opening reveal for both, one hide, then alice draws and discards —
  // deck order and the discard pile now matter.
  for (auto* seat : {&alice, &bob}) {
    for (const int index : {0, 3}) {
      moonbase::golf::PeekCard peek;
      peek.cardIndex = index;
      ASSERT_TRUE(seat->stream.Send(Move(GolfMove::FromPeekcard(peek))).ok());
    }
  }
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromHidecards(moonbase::golf::HideCards{}))).ok());
  ASSERT_TRUE(alice.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      alice.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  // Drain both seats to the same sync point (undelivered frames keep the
  // registry's delivery chain holding the stream through teardown),
  // remembering bob's last full view before the turn handoff — the
  // post-discard fanout, which the restored view must match.
  std::optional<moonbase::golf::GameView> before;
  for (int i = 0; i < 16; ++i) {
    auto update = ReceiveGolf(bob.stream, "gameState");
    if (!update.has_value()) break;
    before.emplace(update->as_gameState_or_null()->view);
    if (before->discardCount == 2) break;  // the post-discard fanout
  }
  ASSERT_TRUE(before.has_value());
  ASSERT_EQ(before->discardCount, 2);
  auto turn = ReceiveGolf(bob.stream, "turnChanged");
  ASSERT_TRUE(turn.has_value());
  EXPECT_EQ(turn->as_turnChanged_or_null()->playerId, bob.player_id);
  ASSERT_TRUE(ReceiveGolf(alice.stream, "turnChanged").has_value());

  const int64_t version_before = [&] {
    auto rows = Rows();
    EXPECT_EQ(rows.games.size(), 1u);
    return rows.games.empty() ? 0 : rows.games[0].version;
  }();
  ASSERT_GT(version_before, 0);

  // The deploy: this process's hub dies, a fresh one boots from the
  // database. Resume tokens are rows (step 1), so the same identities
  // walk back in.
  const std::string alice_token = alice.resume_token;
  const std::string bob_token = bob.resume_token;
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  EXPECT_EQ(alice_back->player_id, alice.player_id);
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(ready->as_sessionReady_or_null()->resumed);
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  EXPECT_EQ(*ready->as_sessionReady_or_null()->roomId, table->room_id);
  ASSERT_TRUE(ReceiveGolf(alice_back->stream, "gameJoined").has_value());

  auto bob_back = OpenSeat(bob_token);
  ASSERT_TRUE(bob_back.has_value());
  EXPECT_EQ(bob_back->player_id, bob.player_id);
  ASSERT_TRUE(ReceiveCase(bob_back->stream, "sessionReady").has_value());
  auto resynced = ReceiveGolf(bob_back->stream, "gameJoined");
  ASSERT_TRUE(resynced.has_value());

  // Not a diorama, and not a re-deal either: bob's restored view matches
  // the one he had — same piles, same discard top, same revealed cards.
  const auto& after = resynced->as_gameJoined_or_null()->view;
  EXPECT_EQ(after.gameId, table->game_id);
  EXPECT_EQ(after.phase, "playing");
  EXPECT_EQ(after.currentPlayerId, bob.player_id);
  EXPECT_EQ(after.drawPileCount, before->drawPileCount);
  EXPECT_EQ(after.discardCount, before->discardCount);
  ASSERT_TRUE(after.discardTop.has_value());
  ASSERT_TRUE(before->discardTop.has_value());
  EXPECT_EQ(after.discardTop->rank, before->discardTop->rank);
  EXPECT_EQ(after.discardTop->suit, before->discardTop->suit);
  for (std::size_t seat = 0; seat < after.players.size(); ++seat) {
    EXPECT_EQ(after.players[seat].revealedIndexes, before->players[seat].revealedIndexes);
    for (std::size_t slot = 0; slot < 4; ++slot) {
      const auto& mine = after.players[seat].cards[slot].card;
      const auto& theirs = before->players[seat].cards[slot].card;
      ASSERT_EQ(mine.has_value(), theirs.has_value());
      if (mine.has_value()) {
        EXPECT_EQ(mine->rank, theirs->rank);
        EXPECT_EQ(mine->suit, theirs->suit);
      }
    }
  }

  // The restored game is live: bob's turn carries on, the move fans out
  // to the restored alice, and the save continues the version sequence —
  // a reset-to-1 restore would be laundered by the repair path and never
  // fail an assertion elsewhere.
  ASSERT_TRUE(bob_back->stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(
      bob_back->stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{}))).ok());
  auto next_turn = ReceiveGolf(alice_back->stream, "turnChanged");
  ASSERT_TRUE(next_turn.has_value());
  EXPECT_EQ(next_turn->as_turnChanged_or_null()->playerId, alice.player_id);
  ASSERT_TRUE(ReceiveGolf(bob_back->stream, "turnChanged").has_value());

  auto rows = Rows();
  ASSERT_EQ(rows.games.size(), 1u);
  EXPECT_EQ(rows.games[0].version, version_before + 2);  // draw + discard
}

TEST_F(PgGolfHubFixture, StatsSurviveARestart) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());

  // Quickest legal game: alice knocks unseen, bob takes his final turn.
  ASSERT_TRUE(table->alice.stream.Send(Move(GolfMove::FromKnock(moonbase::golf::Knock{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "playerKnocked").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDrawcard(moonbase::golf::DrawCard{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameState").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromDiscarddrawn(moonbase::golf::DiscardDrawn{})))
          .ok());
  ASSERT_TRUE(ReceiveGolf(table->alice.stream, "gameEnded").has_value());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameEnded").has_value());

  const std::string alice_token = table->alice.resume_token;
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  ASSERT_TRUE(ReceiveCase(alice_back->stream, "sessionReady").has_value());
  ASSERT_TRUE(
      alice_back->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto lobby = ReceiveCase(alice_back->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  // Identical zero-scoring deals; the knocker takes the tie alone.
  for (const auto& player : lobby->as_roomState_or_null()->players) {
    EXPECT_EQ(player.gamesPlayed, 1);
    EXPECT_EQ(player.totalScore, 0);
    EXPECT_EQ(player.gamesWon, player.playerId == alice_back->player_id ? 1 : 0);
  }
}

TEST_F(PgGolfHubFixture, PendingGameLifecycleWritesThrough) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  // A second, pending game beside the started one: bob leaves his seat in
  // the started game first, creates a fresh lobby game, and its roster
  // rides the row. (Leaving the started two-seat game ends it.)
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  auto created = ReceiveGolf(table->bob.stream, "gameJoined");
  ASSERT_TRUE(created.has_value());
  const std::string pending_id = created->as_gameJoined_or_null()->view.gameId;

  {
    auto rows = Rows();
    ASSERT_EQ(rows.games.size(), 1u);  // the started game ended when bob left
    EXPECT_EQ(rows.games[0].game_id, pending_id);
    EXPECT_FALSE(rows.games[0].state.has_value());
    EXPECT_EQ(rows.games[0].roster, (std::vector<std::string>{table->bob.player_id}));
  }

  // Abandoning the pending game deletes its row and nothing else.
  ASSERT_TRUE(
      table->bob.stream.Send(Move(GolfMove::FromLeavegame(moonbase::golf::LeaveGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(table->bob.stream, "gameLeft").has_value());
  auto rows = Rows();
  EXPECT_TRUE(rows.games.empty());
  EXPECT_EQ(rows.rooms.size(), 1u);
  EXPECT_EQ(rows.members.size(), 2u);
}

TEST_F(PgGolfHubFixture, CorruptRowsLoseTheGameNotTheLobby) {
  auto table = SeatedTable();
  ASSERT_TRUE(table.has_value());
  store_->Flush();

  // A row whose state bytes stopped decoding (schema bump gone wrong,
  // torn write, hand edit): the boot drops the game, keeps the lobby.
  {
    pg::Client db(url_);
    ASSERT_TRUE(db.Exec("UPDATE games SET state = '{\"v\":99}'::jsonb").ok());
  }
  const std::string alice_token = table->alice.resume_token;
  RestartHub();

  auto alice_back = OpenSeat(alice_token);
  ASSERT_TRUE(alice_back.has_value());
  auto ready = ReceiveCase(alice_back->stream, "sessionReady");
  ASSERT_TRUE(ready.has_value());
  ASSERT_TRUE(ready->as_sessionReady_or_null()->roomId.has_value());
  ASSERT_TRUE(
      alice_back->stream.Send(GolfCommands::FromGetroomstate(moonbase::golf::GetRoomState{})).ok());
  auto lobby = ReceiveCase(alice_back->stream, "roomState");
  ASSERT_TRUE(lobby.has_value());
  EXPECT_TRUE(lobby->as_roomState_or_null()->games.empty());
  // Not wedged: the seat can start over.
  ASSERT_TRUE(
      alice_back->stream.Send(Move(GolfMove::FromCreategame(moonbase::golf::CreateGame{}))).ok());
  ASSERT_TRUE(ReceiveGolf(alice_back->stream, "gameJoined").has_value());
}

TEST_F(PgGolfHubFixture, ConnectedFlagFollowsPresence) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());

  {
    auto rows = Rows();
    ASSERT_EQ(rows.members.size(), 1u);
    EXPECT_TRUE(rows.members[0].connected);
  }

  // Across a restart the row keeps its last written value; the member
  // restores disconnected in memory and flips the row back on resume.
  const std::string token = alice->resume_token;
  RestartHub();
  auto alice_back = OpenSeat(token);
  ASSERT_TRUE(alice_back.has_value());
  ASSERT_TRUE(ReceiveCase(alice_back->stream, "sessionReady").has_value());
  auto rows = Rows();
  ASSERT_EQ(rows.members.size(), 1u);
  EXPECT_TRUE(rows.members[0].connected);
}

TEST_F(PgGolfHubFixture, EmptiedRoomVanishesFromTheDatabase) {
  auto alice = OpenSeat();
  ASSERT_TRUE(alice.has_value());
  ASSERT_TRUE(ReceiveCase(alice->stream, "sessionReady").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromCreateroom(moonbase::golf::CreateRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomState").has_value());
  ASSERT_TRUE(alice->stream.Send(GolfCommands::FromLeaveroom(moonbase::golf::LeaveRoom{})).ok());
  ASSERT_TRUE(ReceiveCase(alice->stream, "roomLeft").has_value());

  auto rows = Rows();
  EXPECT_TRUE(rows.rooms.empty());
  EXPECT_TRUE(rows.members.empty());
  EXPECT_TRUE(rows.games.empty());
}

}  // namespace
}  // namespace golf_hub
