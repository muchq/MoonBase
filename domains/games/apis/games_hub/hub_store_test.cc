#include "domains/games/apis/games_hub/hub_store.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "domains/games/apis/games_hub/round_robin.h"
#include "domains/games/libs/cards/card.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/games/libs/chess_play/game_state.h"
#include "domains/games/libs/chess_play/table.h"

namespace games_hub {
namespace {

using ::testing::ElementsAre;

golf::GameState DealtState() {
  std::deque<cards::Card> deck;
  for (int i = 0; i < 52; ++i) deck.emplace_back(i);
  auto dealt = golf::dealGolfGame("G1", {"alice", "bob"}, std::move(deck));
  EXPECT_TRUE(dealt.ok());
  return *std::move(dealt);
}

TEST(MemoryHubStoreTest, OpsRoundTripAndRoomDeleteCascades) {
  MemoryHubStore store;
  store.Enqueue(
      {HubStore::UpsertRoom{"R1"}, HubStore::UpsertMember{{"R1", "alice", true, 2, 1, 9}}});
  store.Flush();
  ASSERT_TRUE(*store.CommitGameSave({"R1", "G1", {"alice"}, std::nullopt, 1}, "ignored"));

  auto snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->rooms.size(), 1u);
  EXPECT_EQ(snapshot->rooms[0].room_id, "R1");
  EXPECT_EQ(snapshot->rooms[0].surface, Surface::Plane());
  ASSERT_EQ(snapshot->members.size(), 1u);
  EXPECT_EQ(snapshot->members[0].player_id, "alice");
  ASSERT_EQ(snapshot->games.size(), 1u);
  EXPECT_EQ(snapshot->games[0].version, 1);

  store.Enqueue({HubStore::DeleteRoom{"R1"}});
  store.Flush();
  snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->rooms.empty());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());

  // Match PostgreSQL's foreign keys: stale child writes cannot recreate
  // rows after their parent room has disappeared.
  store.Enqueue({HubStore::UpsertMember{{"R1", "late", true, 0, 0, 0}}});
  auto landed = store.CommitGameSave({"R1", "late-game", {"late"}, std::nullopt, 1}, "ignored");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);
  snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot->members.empty());
  EXPECT_TRUE(snapshot->games.empty());
}

// The surface a room stands on rides its row (#1554): the create sets
// it, LoadRoom and LoadSnapshot hand it back, a second create of the
// same room (two instances minting one code) keeps the first, and
// SetRoomSurface changes it — for a room that exists; one that does not
// stays absent. A room that chose nothing is a plane.
TEST(MemoryHubStoreTest, RoomSurfaceIsSetByCreateAndChangedBySetRoomSurface) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"S", Surface::Sphere(2)}, HubStore::UpsertRoom{"P"}});
  store.Flush();
  store.Enqueue({HubStore::UpsertRoom{"S", Surface::Plane()},
                 HubStore::SetRoomSurface{"S", Surface::Sphere(53)},
                 HubStore::SetRoomSurface{"ghost", Surface::Sphere(53)}});
  store.Flush();
  EXPECT_FALSE(store.LoadRoom("ghost")->exists);

  auto sphere = store.LoadRoom("S");
  ASSERT_TRUE(sphere.ok());
  EXPECT_EQ(sphere->surface, Surface::Sphere(53));
  auto plane = store.LoadRoom("P");
  ASSERT_TRUE(plane.ok());
  EXPECT_EQ(plane->surface, Surface::Plane());
  auto snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->rooms.size(), 2u);
  EXPECT_EQ(snapshot->rooms[0].room_id, "P");
  EXPECT_EQ(snapshot->rooms[0].surface, Surface::Plane());
  EXPECT_EQ(snapshot->rooms[1].room_id, "S");
  EXPECT_EQ(snapshot->rooms[1].surface, Surface::Sphere(53));
}

TEST(MemoryHubStoreTest, FinishCommitIsConditionalAtomicAndRetained) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"},
                 HubStore::UpsertMember{{"R1", "alice", true, 3, 1, 10}},
                 HubStore::UpsertMember{{"R1", "bob", true, 3, 0, 12}}});
  store.Flush();
  ASSERT_TRUE(*store.CommitGameSave({"R1", "G1", {"alice", "bob"}, std::nullopt, 1}, "start"));

  const golf::GameState state = DealtState();
  const std::vector<HubStore::StatsDelta> deltas = {
      {"alice", 1, 1, 4},
      {"bob", 1, 0, 9},
  };
  auto landed = store.CommitGameFinish({"R1", "G1", {"alice", "bob"}, state, 2}, deltas, "finish");
  ASSERT_TRUE(landed.ok());
  EXPECT_TRUE(*landed);

  auto room = store.LoadRoom("R1");
  ASSERT_TRUE(room.ok());
  ASSERT_EQ(room->games.size(), 1u);
  EXPECT_EQ(room->games[0].version, 2);
  ASSERT_TRUE(room->games[0].state.has_value());
  for (const auto& member : room->members) {
    if (member.player_id == "alice") {
      EXPECT_EQ(member.games_played, 4);
      EXPECT_EQ(member.games_won, 2);
      EXPECT_EQ(member.total_score, 14);
    } else {
      EXPECT_EQ(member.player_id, "bob");
      EXPECT_EQ(member.games_played, 4);
      EXPECT_EQ(member.games_won, 0);
      EXPECT_EQ(member.total_score, 21);
    }
  }

  landed = store.CommitGameFinish({"R1", "G1", {"alice", "bob"}, state, 2}, deltas, "replay");
  ASSERT_TRUE(landed.ok());
  EXPECT_FALSE(*landed);
  room = store.LoadRoom("R1");
  ASSERT_TRUE(room.ok());
  for (const auto& member : room->members) {
    EXPECT_EQ(member.games_played, 4);
  }
}

// White Kg6 Pe7 against Kh8: e7e8q mates.
constexpr char kMate[] = "7k/4P3/6K1/8/8/8/8/8 w - - 0 1";

chess_play::Table ChessOpened(int white_seat = 0) {
  auto table =
      chess_play::Table::open({"alice", "bob"}, "kpk", chess_play::Opening{kMate, white_seat},
                              {180'000, 2'000}, 1'000, "random-kpk");
  EXPECT_TRUE(table.ok()) << table.status();
  return *table;
}

chess_play::Table Mated(const chess_play::Table& table) {
  auto mated = table.inGame([&](const chess_play::GameState& game) {
    return game.move(game.whiteSeat(), "e7e8q", 2'000);
  });
  EXPECT_TRUE(mated.ok()) << mated.status();
  return *mated;
}

chess_play::Table Next(const chess_play::Table& table) {
  auto next =
      table.next("kpk", chess_play::Opening{kMate, 0}, {180'000, 2'000}, 3'000, "random-kpk");
  EXPECT_TRUE(next.ok()) << next.status();
  return *next;
}

HubStore::GameRow ChessRow(const chess_play::Table& table, int64_t version,
                           const std::string& game_id = "G1") {
  HubStore::GameRow row{"R1", game_id, {"alice", "bob"}, table, version};
  row.kind = GameKind::kChess;
  return row;
}

// A room's chess history (#1637) is written by the commit that ends a
// game, in the same step: a game in play is not in it, an ended game is,
// once, however many later commits still carry it, and each game is
// marked with whether the room was published when it ended.
TEST(MemoryHubStoreTest, ACommitThatEndsAChessGameArchivesItOnce) {
  int64_t now = 10'000;
  MemoryHubStore store([&] { return now; });
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  const chess_play::Table opened = ChessOpened();
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(opened, 1), ""));
  auto history = store.LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok()) << history.status();
  EXPECT_FALSE(history->published);
  EXPECT_TRUE(history->games.empty()) << "a game in play is not history";

  const chess_play::Table mated = Mated(opened);
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(mated, 2), ""));
  // A later commit still carrying the ended game: the leave that closes
  // the table between games.
  ASSERT_TRUE(*store.CommitGameFinish(ChessRow(*mated.removePlayer(1, 2'500), 3), {}, ""));
  history = store.LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok());
  ASSERT_EQ(history->games.size(), 1u);
  EXPECT_EQ(history->games[0].game_id, "G1");
  EXPECT_EQ(history->games[0].ordinal, 1);
  EXPECT_EQ(history->games[0].game.moves(), std::vector<std::string>{"e7e8q"});
  EXPECT_EQ(history->games[0].ended_at_ms, 10'000);
  EXPECT_FALSE(history->games[0].published);
}

TEST(MemoryHubStoreTest, ATablesGamesAreArchivedNewestFirstEachWithItsPublishedMark) {
  int64_t now = 10'000;
  MemoryHubStore store([&] { return now; });
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  const chess_play::Table first = Mated(ChessOpened());
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(first, 2), ""));
  store.Enqueue({HubStore::SetChessPublished{"R1", true}});
  now = 20'000;
  const chess_play::Table second = Next(first);
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(second, 3), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(Mated(second), 4), ""));

  auto history = store.LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok());
  EXPECT_TRUE(history->published);
  ASSERT_EQ(history->games.size(), 2u);
  EXPECT_EQ(history->games[0].ordinal, 2);
  EXPECT_TRUE(history->games[0].published);
  EXPECT_EQ(history->games[0].ended_at_ms, 20'000);
  EXPECT_EQ(history->games[1].ordinal, 1);
  EXPECT_FALSE(history->games[1].published);

  history = store.LoadChessHistory("R1", 1);
  ASSERT_TRUE(history.ok());
  ASSERT_EQ(history->games.size(), 1u);
  EXPECT_EQ(history->games[0].ordinal, 2) << "the limit keeps the newest";
}

TEST(MemoryHubStoreTest, AMissedCommitArchivesNothing) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  EXPECT_FALSE(*store.CommitGameSave(ChessRow(Mated(ChessOpened()), 3), ""));
  EXPECT_FALSE(*store.CommitGameFinish(ChessRow(Mated(ChessOpened()), 3), {}, ""));
  EXPECT_TRUE(store.LoadChessHistory("R1", 100)->games.empty());
}

// A table code freed and minted again plays its games under the same
// game id and ordinals; they are other games, and kept.
TEST(MemoryHubStoreTest, AReMintedTableCodeArchivesItsOwnGames) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  store.Enqueue({HubStore::DeleteGame{"R1", "G1"}});
  auto other = chess_play::Table::open({"carol", "dave"}, "kpk", chess_play::Opening{kMate, 0},
                                       {180'000, 2'000}, 1'000, "random-kpk");
  ASSERT_TRUE(other.ok());
  HubStore::GameRow row = ChessRow(*other, 1);
  row.roster = {"carol", "dave"};
  ASSERT_TRUE(*store.CommitGameSave(row, ""));
  row.state = Mated(*other);
  row.version = 2;
  ASSERT_TRUE(*store.CommitGameSave(row, ""));
  EXPECT_EQ(store.LoadChessHistory("R1", 100)->games.size(), 2u);
}

// The public feed (#1637): a game ending in a published room is in it,
// room unnamed; one ending private never is; withdrawing stops the next
// games, not the ones already out; and the feed outlives the room.
TEST(MemoryHubStoreTest, PublishedGamesFeedInArchiveOrderAndOutliveWithdrawalAndTheRoom) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  chess_play::Table table = Mated(ChessOpened());
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(table, 2), ""));  // private
  EXPECT_TRUE(store.LoadPublishedChess(0, 100)->empty());

  store.Enqueue({HubStore::SetChessPublished{"R1", true}});
  int64_t version = 3;
  for (int game = 2; game <= 4; ++game) {
    table = Next(table);
    ASSERT_TRUE(*store.CommitGameSave(ChessRow(table, version++), ""));
    table = Mated(table);
    ASSERT_TRUE(*store.CommitGameSave(ChessRow(table, version++), ""));
  }
  store.Enqueue({HubStore::SetChessPublished{"R1", false}});
  table = Next(table);
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(table, version++), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(Mated(table), version++), ""));  // private again

  const auto history = store.LoadChessHistory("R1", 100);
  ASSERT_TRUE(history.ok());
  ASSERT_EQ(history->games.size(), 5u);
  auto feed = store.LoadPublishedChess(0, 100);
  ASSERT_TRUE(feed.ok());
  ASSERT_EQ(feed->size(), 3u) << "games 2 to 4: withdrawing keeps what was out";
  EXPECT_EQ((*feed)[0].archive_id, history->games[3].archive_id) << "archive order";
  EXPECT_EQ((*feed)[2].archive_id, history->games[1].archive_id);
  EXPECT_LT((*feed)[0].archive_id, (*feed)[1].archive_id);

  feed = store.LoadPublishedChess((*feed)[0].archive_id, 100);
  ASSERT_TRUE(feed.ok());
  EXPECT_EQ(feed->size(), 2u) << "after is exclusive";
  feed = store.LoadPublishedChess(0, 1);
  ASSERT_TRUE(feed.ok());
  EXPECT_EQ(feed->size(), 1u);

  store.Enqueue({HubStore::DeleteRoom{"R1"}});
  EXPECT_EQ(store.LoadPublishedChess(0, 100)->size(), 3u) << "the feed outlives the room";
}

TEST(MemoryHubStoreTest, ThePublishedFeedIsSweptByAge) {
  int64_t now = 10'000;
  MemoryHubStore store([&] { return now; });
  store.Enqueue({HubStore::UpsertRoom{"R1"}, HubStore::SetChessPublished{"R1", true}});
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  now = 70'000;
  store.Enqueue({HubStore::SweepPublishedChess{std::chrono::seconds(60)}});
  EXPECT_EQ(store.LoadPublishedChess(0, 100)->size(), 1u) << "60s old exactly stays";
  now = 70'001;
  store.Enqueue({HubStore::SweepPublishedChess{std::chrono::seconds(60)}});
  EXPECT_TRUE(store.LoadPublishedChess(0, 100)->empty());
  EXPECT_EQ(store.LoadChessHistory("R1", 100)->games.size(), 1u) << "the room keeps its own";
}

// A game is named by its id, or by table code and line, which a re-minted
// code shares with an older game: that names the newest.
TEST(MemoryHubStoreTest, AnArchivedGameIsFoundByIdOrByTableAndLine) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}, HubStore::UpsertRoom{"R2"}});
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(ChessOpened(), 1), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(Mated(ChessOpened()), 2), ""));
  store.Enqueue({HubStore::DeleteGame{"R1", "G1"}});
  auto other = chess_play::Table::open({"carol", "dave"}, "kpk", chess_play::Opening{kMate, 0},
                                       {180'000, 2'000}, 1'000, "random-kpk");
  ASSERT_TRUE(other.ok());
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(*other, 1), ""));
  ASSERT_TRUE(*store.CommitGameSave(ChessRow(Mated(*other), 2), ""));
  const auto history = store.LoadChessHistory("R1", 100);
  ASSERT_EQ(history->games.size(), 2u);
  const int64_t older = history->games[1].archive_id;
  const int64_t newer = history->games[0].archive_id;

  auto found = store.LoadChessGame("R1", {older, "", 0});
  ASSERT_TRUE(found.ok() && found->has_value());
  EXPECT_EQ((*found)->archive_id, older);
  found = store.LoadChessGame("R1", {std::nullopt, "G1", 1});
  ASSERT_TRUE(found.ok() && found->has_value());
  EXPECT_EQ((*found)->archive_id, newer);
  EXPECT_FALSE(store.LoadChessGame("R1", {std::nullopt, "G1", 2})->has_value());
  EXPECT_FALSE(store.LoadChessGame("R2", {older, "", 0})->has_value()) << "another room's game";
  EXPECT_FALSE(store.LoadChessGame("ghost", {older, "", 0})->has_value());
}

TEST(MemoryHubStoreTest, ThePublishedFlagRidesTheRoomRows) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}, HubStore::SetChessPublished{"R1", true},
                 HubStore::SetChessPublished{"ghost", true}});
  EXPECT_TRUE(store.LoadRoom("R1")->chess_published);
  auto snapshot = store.LoadSnapshot();
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot->rooms.size(), 1u) << "publishing no room makes none";
  EXPECT_TRUE(snapshot->rooms[0].chess_published);
}

HubStore::ChessEventRow Event(int64_t version, const std::string& creator = "alice") {
  HubStore::ChessEventRow row;
  row.room_id = "R1";
  row.event_id = "E1";
  row.version = version;
  row.creator = creator;
  row.entrants = {"alice", "bob", "carol"};
  row.terms = {"standard", {180'000, 2'000}};
  row.pairings = *RoundRobinPairings(row.entrants);
  return row;
}

// A round robin (#1647) commits as a games row does: version 1 creates
// it, refused when the id is taken or the room is not; each later
// version replaces the one before, and nothing else lands.
TEST(MemoryHubStoreTest, AnEventCommitsOnItsVersionAndDiesWithItsRoom) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  ASSERT_TRUE(*store.CommitChessEvent(Event(1), ""));
  EXPECT_FALSE(*store.CommitChessEvent(Event(1, "mallory"), "")) << "the id is taken";
  HubStore::ChessEventRow ghost = Event(1);
  ghost.room_id = "ghost";
  EXPECT_FALSE(*store.CommitChessEvent(ghost, "")) << "no such room";

  HubStore::ChessEventRow moderated = Event(2);
  moderated.withdrawn = {"carol"};
  moderated.pairings[0].result = PairingResult::kWhite;
  moderated.pairings[0].forfeit = true;
  EXPECT_FALSE(*store.CommitChessEvent(Event(3), "")) << "skips a version";
  ASSERT_TRUE(*store.CommitChessEvent(moderated, ""));
  EXPECT_FALSE(*store.CommitChessEvent(Event(2), "")) << "replaces 1, not 2";

  auto room = store.LoadRoom("R1");
  ASSERT_TRUE(room.ok());
  ASSERT_EQ(room->events.size(), 1u);
  EXPECT_EQ(room->events[0].version, 2);
  EXPECT_EQ(room->events[0].creator, "alice");
  EXPECT_EQ(room->events[0].entrants, moderated.entrants);
  EXPECT_EQ(room->events[0].terms, moderated.terms);
  EXPECT_EQ(room->events[0].pairings, moderated.pairings);
  EXPECT_EQ(room->events[0].withdrawn, moderated.withdrawn);
  ASSERT_EQ(store.LoadSnapshot()->events.size(), 1u);

  store.Enqueue({HubStore::DeleteRoom{"R1"}});
  EXPECT_TRUE(store.LoadSnapshot()->events.empty());
}

// White to move; b5b6 stalemates the king on a8.
constexpr char kStalemate[] = "k7/8/2K5/1Q6/8/8/8/8 w - - 0 1";

chess_play::Table Drawn() {
  auto table = chess_play::Table::open({"alice", "bob"}, "kpk", chess_play::Opening{kStalemate, 0},
                                       {180'000, 2'000}, 1'000, "random-kpk");
  EXPECT_TRUE(table.ok()) << table.status();
  auto drawn = table->inGame([&](const chess_play::GameState& game) {
    return game.move(game.whiteSeat(), "b5b6", 2'000);
  });
  EXPECT_TRUE(drawn.ok()) << drawn.status();
  EXPECT_TRUE(drawn->game().isOver());
  return *drawn;
}

HubStore::GameRow Tagged(const chess_play::Table& table, int64_t version,
                         const std::string& game_id, std::optional<EventTag> tag) {
  HubStore::GameRow row = ChessRow(table, version, game_id);
  row.event = std::move(tag);
  return row;
}

// A table playing a pairing ends its first game in the commit that
// archives it, and that game is the pairing's: the event lists it with
// its winner (none for a draw) under the tag the table was made with. A
// rematch on the table is not the pairing's, and an untagged table's game
// is history, not the event's.
TEST(MemoryHubStoreTest, ATaggedTablesFirstGameIsArchivedAsItsPairings) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  ASSERT_TRUE(*store.CommitChessEvent(Event(1), ""));
  const EventTag tag{"E1", 2};
  // White in the second seat: the winner is named by seat, not by order.
  const chess_play::Table opened = ChessOpened(1);
  ASSERT_TRUE(*store.CommitGameSave(Tagged(opened, 1, "G1", tag), ""));
  EXPECT_EQ((*store.LoadGame("R1", "G1"))->event, std::optional<EventTag>(tag));

  const chess_play::Table mated = Mated(opened);
  ASSERT_TRUE(*store.CommitGameSave(Tagged(mated, 2, "G1", EventTag{"E9", 0}), ""));
  EXPECT_EQ((*store.LoadGame("R1", "G1"))->event, std::optional<EventTag>(tag))
      << "only the insert writes the tag";
  ASSERT_TRUE(*store.CommitGameSave(Tagged(Next(mated), 3, "G1", std::nullopt), ""));
  const chess_play::Table rematched = Mated(Next(mated));
  ASSERT_TRUE(*store.CommitGameSave(Tagged(rematched, 4, "G1", std::nullopt), ""));
  ASSERT_TRUE(*store.CommitGameSave(Tagged(Next(rematched), 5, "G1", std::nullopt), ""));
  ASSERT_TRUE(
      *store.CommitGameFinish(Tagged(Mated(Next(rematched)), 6, "G1", std::nullopt), {}, ""));
  ASSERT_TRUE(*store.CommitGameSave(Tagged(opened, 1, "G2", std::nullopt), ""));
  ASSERT_TRUE(*store.CommitGameSave(Tagged(mated, 2, "G2", std::nullopt), ""));
  const chess_play::Table drawn = Drawn();
  ASSERT_TRUE(*store.CommitGameSave(Tagged(drawn, 1, "G3", EventTag{"E1", 0}), ""));
  ASSERT_TRUE(*store.CommitGameFinish(Tagged(*drawn.removePlayer(0, 3'000), 2, "G3", std::nullopt),
                                      {}, ""));

  ASSERT_EQ(store.LoadChessHistory("R1", 100)->games.size(), 5u);
  const auto archived = [&](const std::string& game_id, int ordinal) {
    return (*store.LoadChessGame("R1", {std::nullopt, game_id, ordinal}))->archive_id;
  };
  auto room = store.LoadRoom("R1");
  ASSERT_TRUE(room.ok());
  ASSERT_EQ(room->events.size(), 1u);
  ASSERT_EQ(opened.players()[opened.game().whiteSeat()], "bob");
  EXPECT_THAT(room->events[0].games,
              ElementsAre(HubStore::EventGame{archived("G1", 1), "G1", 2, "bob"},
                          HubStore::EventGame{archived("G3", 1), "G3", 0, std::nullopt}))
      << "archive order";

  // Each event holds its own games: another event in the room, and one
  // of the same id in another room, each with a game of its own.
  store.Enqueue({HubStore::UpsertRoom{"R2"}});
  HubStore::ChessEventRow second = Event(1);
  second.event_id = "E2";
  HubStore::ChessEventRow elsewhere = Event(1);
  elsewhere.room_id = "R2";
  ASSERT_TRUE(*store.CommitChessEvent(second, ""));
  ASSERT_TRUE(*store.CommitChessEvent(elsewhere, ""));
  ASSERT_TRUE(*store.CommitGameSave(Tagged(opened, 1, "G4", EventTag{"E2", 1}), ""));
  ASSERT_TRUE(*store.CommitGameSave(Tagged(mated, 2, "G4", std::nullopt), ""));
  HubStore::GameRow r2 = Tagged(opened, 1, "G1", EventTag{"E1", 1});
  r2.room_id = "R2";
  ASSERT_TRUE(*store.CommitGameSave(r2, ""));
  HubStore::GameRow r2_ended = ChessRow(mated, 2);
  r2_ended.room_id = "R2";
  ASSERT_TRUE(*store.CommitGameSave(r2_ended, ""));

  // An event loaded with its games and committed back keeps them as the
  // archive has them: the commit writes none.
  HubStore::ChessEventRow reloaded = store.LoadRoom("R1")->events[0];
  reloaded.version = 2;
  ASSERT_TRUE(*store.CommitChessEvent(reloaded, ""));

  room = store.LoadRoom("R1");
  ASSERT_EQ(room->events.size(), 2u);
  EXPECT_EQ(room->events[0].games.size(), 2u);
  ASSERT_EQ(room->events[1].games.size(), 1u);
  EXPECT_EQ(room->events[1].games[0].game_id, "G4");
  auto snapshot = store.LoadSnapshot();
  ASSERT_EQ(snapshot->events.size(), 3u);
  for (const HubStore::ChessEventRow& event : snapshot->events) {
    const size_t want = event.room_id == "R1" && event.event_id == "E1" ? 2u : 1u;
    EXPECT_EQ(event.games.size(), want) << event.room_id << "/" << event.event_id;
  }
}

// Two tables racing to play one pairing each end a first game, and the
// event lists both, in archive order: the store does not pick.
TEST(MemoryHubStoreTest, TwoTablesPlayingOnePairingAreBothListed) {
  MemoryHubStore store;
  store.Enqueue({HubStore::UpsertRoom{"R1"}});
  ASSERT_TRUE(*store.CommitChessEvent(Event(1), ""));
  const chess_play::Table opened = ChessOpened();
  for (const std::string game_id : {"G1", "G2"}) {
    ASSERT_TRUE(*store.CommitGameSave(Tagged(opened, 1, game_id, EventTag{"E1", 1}), ""));
    ASSERT_TRUE(*store.CommitGameSave(Tagged(Mated(opened), 2, game_id, std::nullopt), ""));
  }
  const auto games = store.LoadRoom("R1")->events[0].games;
  ASSERT_EQ(games.size(), 2u);
  EXPECT_EQ(games[0].game_id, "G1");
  EXPECT_EQ(games[1].game_id, "G2");
  EXPECT_EQ(games[0].pairing, 1);
  EXPECT_EQ(games[1].pairing, 1);
  EXPECT_LT(games[0].archive_id, games[1].archive_id);
}

}  // namespace
}  // namespace games_hub
