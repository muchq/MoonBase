// Wire-contract goldens for rummy (#245) on the room stream, the way
// castle_wire_test pins castle's: raw eventstream frames and exact payload
// bytes, because the typed-client suites regenerate both sides together
// and cannot see a rename. The pinned surface: the rummy command envelope
// ({"move":{...}} inside the `rummy` command) with every move's spelling,
// the update envelope ({"update":{...}} inside the `rummy` event), the
// dealer's-choice table (#1609) between deals and dealt, the dealt
// RummyView's full key set from each chair, the view mid-turn (stage,
// takenDiscard, melds, lastMove), a deal's end, and the table's.
//
// The NoShuffleDealer deals one card a seat from the back of the pristine
// deck: player-1 (the creator, who deals first) holds A♠ A♦ K♠ K♦ Q♠ Q♦
// J♠, player-2 (who opens) A♥ A♣ K♥ K♣ Q♥ Q♣ J♥; J♦ is turned up and J♣
// tops the stock.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "opal/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

constexpr char kPlayPath[] = "/games/v2/play";

class RummyWireTest : public HubWireFixture {
 protected:
  std::shared_ptr<opal::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }

  // Two seats at a started table, between deals: every frame up to both
  // chairs' choosing views and the room's listing of them read.
  void ChoosingTable(std::shared_ptr<opal::http::WebSocket>& creator,
                     std::shared_ptr<opal::http::WebSocket>& joiner) {
    json creator_session;
    creator = DialReady(creator_session);
    EXPECT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
    (void)EventPayload(NextFrame(*creator), "roomState");
    EXPECT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"createGame":{}}})")).ok());
    (void)EventPayload(NextFrame(*creator), "rummy");  // gameCreated
    (void)EventPayload(NextFrame(*creator), "rummy");  // gameJoined
    (void)EventPayload(NextFrame(*creator), "roomState");
    json joiner_session;
    joiner = DialReady(joiner_session);
    EXPECT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
    (void)EventPayload(NextFrame(*joiner), "roomState");
    (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
    (void)EventPayload(NextFrame(*creator), "roomState");
    EXPECT_TRUE(
        joiner->Send(CommandFrame("rummy", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
    (void)EventPayload(NextFrame(*joiner), "rummy");  // gameJoined
    (void)EventPayload(NextFrame(*joiner), "roomState");
    (void)EventPayload(NextFrame(*creator), "rummy");  // gameState
    (void)EventPayload(NextFrame(*creator), "roomState");
    EXPECT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"startGame":{}}})")).ok());
    for (auto* socket : {creator.get(), joiner.get()}) {
      EXPECT_EQ(EventPayload(NextFrame(*socket), "rummy"), R"({"update":{"gameStarted":{}}})");
      (void)EventPayload(NextFrame(*socket), "rummy");  // the choosing view
      (void)EventPayload(NextFrame(*socket), "roomState");
    }
  }

  // The same table with seven-card dealt by player-1: the creator's frames read
  // up to its dealt view, which is returned; the room's listing and the
  // opening turn follow it. The joiner's are all still to read.
  std::string DealtTable(std::shared_ptr<opal::http::WebSocket>& creator,
                         std::shared_ptr<opal::http::WebSocket>& joiner) {
    ChoosingTable(creator, joiner);
    EXPECT_TRUE(
        creator->Send(CommandFrame("rummy", R"({"move":{"chooseVariant":{"variant":"7-card"}}})"))
            .ok());
    const std::string dealt = EventPayload(NextFrame(*creator), "rummy");
    (void)EventPayload(NextFrame(*creator), "roomState");
    EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
              R"({"update":{"turnChanged":{"playerId":"player-2"}}})");
    return dealt;
  }

  // DealtTable, with the joiner's dealt frames read too.
  void DealtAndRead(std::shared_ptr<opal::http::WebSocket>& creator,
                    std::shared_ptr<opal::http::WebSocket>& joiner) {
    (void)DealtTable(creator, joiner);
    (void)EventPayload(NextFrame(*joiner), "rummy");  // the dealt view
    (void)EventPayload(NextFrame(*joiner), "roomState");
    (void)EventPayload(NextFrame(*joiner), "rummy");  // turnChanged
  }
};

TEST_F(RummyWireTest, CreateAndJoinPinTheWaitingViewAndTheLobbysWord) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");

  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"gameJoined":{"view":{"canDrawDiscard":false,"canDrawStock":false,)"
            R"("dealNumber":0,)"
            R"("discardCount":0,"discardPile":[],"gameId":"GAME01","melds":[],)"
            R"("phase":"waiting","players":[{"hand":[],"handCount":0,"playerId":"player-1"}],)"
            R"("scoreSheet":[],"standings":[{"handsWon":0,"playerId":"player-1","points":0}],)"
            R"("stockCount":0}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"rummy","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"rummy","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");
}

// Started, the table is between deals: the dealer and the variants on
// offer, no cards, and the room lists it as choosing. Only the dealer
// deals, and only a variant the table offers.
TEST_F(RummyWireTest, AStartedTableWaitsOnTheDealersChoice) {
  json creator_session;
  auto creator = DialReady(creator_session);
  // ChoosingTable reads the choosing frames; this test pins them, so it
  // walks the same way and stops short.
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");
  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"createGame":{}}})")).ok());
  for (int i = 0; i < 3; ++i) (void)NextFrame(*creator);
  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  for (int i = 0; i < 2; ++i) (void)NextFrame(*joiner);
  (void)NextFrame(*creator);
  ASSERT_TRUE(
      joiner->Send(CommandFrame("rummy", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  for (int i = 0; i < 2; ++i) (void)NextFrame(*joiner);
  for (int i = 0; i < 2; ++i) (void)NextFrame(*creator);

  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"startGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"), R"({"update":{"gameStarted":{}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"gameState":{"view":{"canDrawDiscard":false,"canDrawStock":false,)"
            R"("choosing":{"dealer":"player-1","options":["7-card","10-card","gin"]},)"
            R"("dealNumber":0,"discardCount":0,"discardPile":[],"gameId":)"
            R"("GAME01","melds":[],"phase":"choosing","players":[{"hand":[],"handCount":0,)"
            R"("playerId":"player-1"},{"hand":[],"handCount":0,"playerId":"player-2"}],)"
            R"("scoreSheet":[],"standings":[{"handsWon":0,"playerId":"player-1","points":0},)"
            R"({"handsWon":0,"playerId":"player-2","points":0}],"stockCount":0}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"rummy","gameId":"GAME01","playerCount":2,"status":"choosing"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"rummy","gameId":"GAME01"},"totalScore":0},)"
            R"({"connected":true,"gamesPlayed":0,"gamesWon":0,"playerId":"player-2","table":)"
            R"({"game":"rummy","gameId":"GAME01"},"totalScore":0}],"roomId":"room-1"})");
  for (int i = 0; i < 3; ++i) (void)NextFrame(*joiner);

  ASSERT_TRUE(
      joiner->Send(CommandFrame("rummy", R"({"move":{"chooseVariant":{"variant":"7-card"}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "commandRejected"),
            R"({"reason":"the dealer chooses"})");
  ASSERT_TRUE(
      creator->Send(CommandFrame("rummy", R"({"move":{"chooseVariant":{"variant":"canasta"}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"({"reason":"no such game: canasta"})");
}

TEST_F(RummyWireTest, TheDealtViewFromEachChair) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  const std::string dealt = DealtTable(creator, joiner);
  EXPECT_EQ(
      dealt,
      R"({"update":{"gameState":{"view":{"canDrawDiscard":true,"canDrawStock":true,)"
      R"("currentPlayerId":"player-2",)"
      R"("dealNumber":1,"discardCount":1,"discardPile":[{"rank":"J","suit":"♦"}],)"
      R"("discardTop":{"rank":"J","suit":"♦"},"gameId":"GAME01",)"
      R"("melds":[],"phase":"playing","players":[{"hand":[{"rank":"A","suit":"♠"},)"
      R"({"rank":"A","suit":"♦"},{"rank":"K","suit":"♠"},{"rank":"K","suit":"♦"},)"
      R"({"rank":"Q","suit":"♠"},{"rank":"Q","suit":"♦"},{"rank":"J","suit":"♠"}],)"
      R"("handCount":7,"playerId":"player-1"},{"hand":[],"handCount":7,"playerId":"player-2"}],)"
      R"("scoreSheet":[],"stage":"draw","standings":[{"handsWon":0,"playerId":"player-1",)"
      R"("points":0},{"handsWon":0,"playerId":"player-2","points":0}],"stockCount":37,)"
      R"("variant":"7-card"}}}})");
  const json view = json::parse(dealt)["update"]["gameState"]["view"];
  EXPECT_EQ(KeysOf(view),
            (std::set<std::string>{"canDrawDiscard", "canDrawStock", "currentPlayerId",
                                   "dealNumber", "discardCount", "discardPile", "discardTop",
                                   "gameId", "melds", "phase", "players", "scoreSheet", "stage",
                                   "standings", "stockCount", "variant"}));
  EXPECT_EQ(KeysOf(view["players"][0]), (std::set<std::string>{"hand", "handCount", "playerId"}));
  // Absent optionals are omitted keys, not nulls: nothing taken, no move
  // yet, no deal before this one, nobody choosing.
  for (const char* absent : {"takenDiscard", "lastMove", "lastDeal", "choosing"}) {
    EXPECT_FALSE(view.contains(absent)) << absent;
  }

  // The other chair: its own faces, the dealer's as a count.
  const json theirs =
      json::parse(EventPayload(NextFrame(*joiner), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(theirs["players"][0]["hand"], json::array());
  EXPECT_EQ(theirs["players"][0]["handCount"], 7);
  EXPECT_EQ(theirs["players"][1]["hand"][0], (json{{"rank", "A"}, {"suit", "♥"}}));
  // The room lists the table in play.
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*joiner), "roomState"))["games"][0]["status"],
            "playing");
}

// Every in-game move's spelling, and what the table sees of each, down to
// the quickest discard-draw win the deck allows and the deal's end.
TEST_F(RummyWireTest, TurnMovesPinTheirSpellingAndTheDealsEndBytes) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  DealtAndRead(creator, joiner);

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"drawDiscard":{}}})")).ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"gameState":{"view":{"canDrawDiscard":false,"canDrawStock":true,)"
            R"("currentPlayerId":"player-2","dealNumber":1,"discardCount":0,"discardPile":[],)"
            R"("gameId":"GAME01","lastMove":{"cards":[{"rank":)"
            R"("J","suit":"♦"}],"move":"drawDiscard","playerId":"player-2"},"melds":[],"phase":)"
            R"("playing","players":[{"hand":[{"rank":"A","suit":"♠"},{"rank":"A","suit":"♦"},)"
            R"({"rank":"K","suit":"♠"},{"rank":"K","suit":"♦"},{"rank":"Q","suit":"♠"},)"
            R"({"rank":"Q","suit":"♦"},{"rank":"J","suit":"♠"}],"handCount":7,"playerId":)"
            R"("player-1"},{"hand":[],"handCount":8,"playerId":"player-2"}],"scoreSheet":[],)"
            R"("stage":"play","standings":[{"handsWon":0,"playerId":"player-1","points":0},)"
            R"({"handsWon":0,"playerId":"player-2","points":0}],"stockCount":37,)"
            R"("takenDiscard":{"rank":"J","suit":"♦"},)"
            R"("variant":"7-card"}}}})");

  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame(
              "rummy",
              R"({"move":{"meld":{"cards":[{"rank":"K","suit":"♥"},{"rank":"Q","suit":"♥"},)"
              R"({"rank":"J","suit":"♥"}]}}})"))
          .ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  const json melded = json::parse(EventPayload(NextFrame(*creator), "rummy"));
  EXPECT_EQ(melded["update"]["gameState"]["view"]["melds"].dump(),
            R"([{"cards":[{"rank":"J","suit":"♥"},{"rank":"Q","suit":"♥"},)"
            R"({"rank":"K","suit":"♥"}],"owner":"player-2"}])");
  EXPECT_EQ(melded["update"]["gameState"]["view"]["lastMove"].dump(),
            R"({"cards":[{"rank":"J","suit":"♥"},{"rank":"Q","suit":"♥"},{"rank":"K","suit":"♥"}],)"
            R"("meldIndex":0,"move":"meld","playerId":"player-2"})");

  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame(
              "rummy", R"({"move":{"layOff":{"card":{"rank":"A","suit":"♥"},"meldIndex":0}}})"))
          .ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*creator),
                                     "rummy"))["update"]["gameState"]["view"]["lastMove"]
                .dump(),
            R"({"cards":[{"rank":"A","suit":"♥"}],"meldIndex":0,"move":"layOff","playerId":)"
            R"("player-2"})");

  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame(
              "rummy",
              R"({"move":{"meld":{"cards":[{"rank":"A","suit":"♣"},{"rank":"K","suit":"♣"},)"
              R"({"rank":"Q","suit":"♣"}]}}})"))
          .ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*creator), "rummy");

  // J♦ is all she holds: the card she took may go back as her last, and
  // the deal is hers. The table turns to the next deal, player-2 dealing;
  // the deal's result rides the view, and nothing ends.
  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame("rummy", R"({"move":{"discard":{"card":{"rank":"J","suit":"♦"}}}})"))
          .ok());
  const json between =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(between["phase"], "choosing");
  EXPECT_EQ(between["choosing"].dump(),
            R"({"dealer":"player-2","options":["7-card","10-card","gin"]})");
  EXPECT_EQ(between["lastDeal"].dump(),
            R"({"points":52,"scores":[{"deadwood":52,"playerId":"player-1"},)"
            R"({"deadwood":0,"playerId":"player-2"}],"variant":"7-card","winner":"player-2"})");
  EXPECT_EQ(between["standings"].dump(),
            R"([{"handsWon":0,"playerId":"player-1","points":0},{"handsWon":1,"playerId":)"
            R"("player-2","points":52}])");
  // The deal goes on the sheet; the standings' points are its sums.
  EXPECT_EQ(between["scoreSheet"].dump(),
            R"([{"points":52,"variant":"7-card","winner":"player-2"}])");
  for (const char* absent : {"currentPlayerId", "stage", "takenDiscard"}) {
    EXPECT_FALSE(between.contains(absent)) << absent;
  }
  EXPECT_EQ(between["canDrawStock"], false);
  EXPECT_EQ(between["canDrawDiscard"], false);
  EXPECT_EQ(between["dealNumber"], 1);
  EXPECT_EQ(between["players"][0]["hand"].size(), 7u);
  const json room = json::parse(EventPayload(NextFrame(*creator), "roomState"));
  EXPECT_EQ(room["games"][0]["status"], "choosing");
  EXPECT_EQ(room["players"][1]["gamesWon"], 1);
  // Between deals the view keeps the last deal's variant and cards, and
  // adds who deals next and how the last deal went.
  EXPECT_EQ(KeysOf(between),
            (std::set<std::string>{"canDrawDiscard", "canDrawStock", "choosing", "dealNumber",
                                   "discardCount", "discardPile", "discardTop", "gameId",
                                   "lastDeal", "lastMove", "melds", "phase", "players",
                                   "scoreSheet", "standings", "stockCount", "variant"}));
  EXPECT_EQ(between["variant"], "7-card");

  // The loser leaving closes the table: the standings it ends on keep the
  // points the sheet scored.
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*joiner), "roomState");
  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"leaveGame":{}}})")).ok());
  const json closed =
      json::parse(EventPayload(NextFrame(*joiner), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(closed["scoreSheet"], between["scoreSheet"]);
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "rummy"),
            R"({"update":{"gameEnded":{"dealsPlayed":1,"standings":[{"handsWon":1,"playerId":)"
            R"("player-2","points":52}]}}})");
}

// Below two seats the table closes: the last deal, broken up, names no
// winner — the key is omitted, never null, which is what the UI reads as
// nobody having gone out — and gameEnded carries the table's standings.
TEST_F(RummyWireTest, ALeaveMidDealClosesTheTableWithItsStandings) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  DealtAndRead(creator, joiner);

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"leaveGame":{}}})")).ok());
  const json closed =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(closed["phase"], "ended");
  EXPECT_FALSE(closed.contains("choosing"));
  EXPECT_EQ(closed["lastDeal"].dump(),
            R"({"points":0,"scores":[{"deadwood":52,"playerId":"player-1"}],"variant":"7-card"})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"gameEnded":{"dealsPlayed":1,"standings":[{"handsWon":0,"playerId":)"
            R"("player-1","points":0}]}}})");
}

// Gin's moves and its end. Dealt ten a seat, player-1 holds 10-A in ♠
// and ♦, player-2 10-A♥ and 10-K♣ with A♣ over; 9♠ is turned up and 9♥
// tops the stock. Both pass the upcard, player-2 draws 9♥ and knocks on
// A♣ with nothing left over: gin, 25 and player-1's deadwood (none), and
// nothing laid off after gin.
TEST_F(RummyWireTest, GinPassesKnocksAndReckons) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  ChoosingTable(creator, joiner);
  ASSERT_TRUE(
      creator->Send(CommandFrame("rummy", R"({"move":{"chooseVariant":{"variant":"gin"}}})")).ok());
  const json dealt =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(dealt["variant"], "gin");
  EXPECT_EQ(dealt["stage"], "upcard");
  EXPECT_EQ(dealt["canDrawStock"], false);
  EXPECT_EQ(dealt["canDrawDiscard"], true);
  EXPECT_EQ(dealt["stockCount"], 31);
  EXPECT_EQ(dealt["discardTop"].dump(), R"({"rank":"9","suit":"♠"})");
  EXPECT_EQ(dealt["players"][0]["handCount"], 10);
  (void)EventPayload(NextFrame(*creator), "roomState");
  (void)EventPayload(NextFrame(*creator), "rummy");  // turnChanged
  for (int i = 0; i < 3; ++i) (void)NextFrame(*joiner);

  // A move gin does not have is refused by name.
  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame(
              "rummy",
              R"({"move":{"meld":{"cards":[{"rank":"K","suit":"♥"},{"rank":"Q","suit":"♥"},)"
              R"({"rank":"J","suit":"♥"}]}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "commandRejected"),
            R"({"reason":"not a move in gin"})");
  // Nor does gin take the pile down: the top card or nothing.
  ASSERT_TRUE(joiner
                  ->Send(CommandFrame(
                      "rummy", R"({"move":{"drawDiscard":{"card":{"rank":"K","suit":"♣"}}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "commandRejected"),
            R"({"reason":"gin takes only the discard pile's top card"})");

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"pass":{}}})")).ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*joiner), "rummy");  // turnChanged
  const json passed =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(passed["lastMove"].dump(), R"({"cards":[],"move":"pass","playerId":"player-2"})");
  EXPECT_EQ(passed["stage"], "upcard");
  EXPECT_EQ(passed["currentPlayerId"], "player-1");
  (void)EventPayload(NextFrame(*creator), "rummy");  // turnChanged

  // Passed by both, the opener draws from the stock and only there.
  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"pass":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  (void)EventPayload(NextFrame(*creator), "rummy");  // turnChanged
  const json stockOnly =
      json::parse(EventPayload(NextFrame(*joiner), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(stockOnly["stage"], "draw");
  EXPECT_EQ(stockOnly["canDrawStock"], true);
  EXPECT_EQ(stockOnly["canDrawDiscard"], false);
  (void)EventPayload(NextFrame(*joiner), "rummy");  // turnChanged

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"drawStock":{}}})")).ok());
  const json drew =
      json::parse(EventPayload(NextFrame(*joiner), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(drew["players"][1]["hand"][10].dump(), R"({"rank":"9","suit":"♥"})");
  (void)EventPayload(NextFrame(*creator), "rummy");

  ASSERT_TRUE(
      joiner->Send(CommandFrame("rummy", R"({"move":{"knock":{"card":{"rank":"A","suit":"♣"}}}})"))
          .ok());
  const json between =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(between["phase"], "choosing");
  EXPECT_EQ(between["lastMove"].dump(),
            R"({"cards":[{"rank":"A","suit":"♣"}],"move":"knock","playerId":"player-2"})");
  EXPECT_EQ(between["lastDeal"].dump(),
            R"({"gin":{"ending":"gin","hands":[{"deadwood":[],"melds":[[{"rank":"10","suit":"♠"},)"
            R"({"rank":"J","suit":"♠"},{"rank":"Q","suit":"♠"},{"rank":"K","suit":"♠"},)"
            R"({"rank":"A","suit":"♠"}],[{"rank":"10","suit":"♦"},{"rank":"J","suit":"♦"},)"
            R"({"rank":"Q","suit":"♦"},{"rank":"K","suit":"♦"},{"rank":"A","suit":"♦"}]],)"
            R"("playerId":"player-1"},{"deadwood":[],"melds":[[{"rank":"Q","suit":"♥"},)"
            R"({"rank":"K","suit":"♥"},{"rank":"A","suit":"♥"}],[{"rank":"10","suit":"♣"},)"
            R"({"rank":"J","suit":"♣"},{"rank":"Q","suit":"♣"},{"rank":"K","suit":"♣"}],)"
            R"([{"rank":"9","suit":"♥"},{"rank":"10","suit":"♥"},{"rank":"J","suit":"♥"}]],)"
            R"("playerId":"player-2"}],"knocker":"player-2","laidOff":[]},"points":25,)"
            R"("scores":[{"deadwood":0,"playerId":"player-1"},{"deadwood":0,"playerId":)"
            R"("player-2"}],"variant":"gin","winner":"player-2"})");
  EXPECT_EQ(between["standings"].dump(),
            R"([{"handsWon":0,"playerId":"player-1","points":0},{"handsWon":1,"playerId":)"
            R"("player-2","points":25}])");
}

// The same deal, player-2 taking the upcard 9♠ and knocking on K♣ with
// 9♠ and A♣ over (10): player-1's 10-A runs leave nothing, which
// undercuts — player-1 scores 25 and the 10.
TEST_F(RummyWireTest, GinUndercut) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  ChoosingTable(creator, joiner);
  ASSERT_TRUE(
      creator->Send(CommandFrame("rummy", R"({"move":{"chooseVariant":{"variant":"gin"}}})")).ok());
  for (int i = 0; i < 3; ++i) (void)NextFrame(*creator);
  for (int i = 0; i < 3; ++i) (void)NextFrame(*joiner);

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"drawDiscard":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  ASSERT_TRUE(
      joiner->Send(CommandFrame("rummy", R"({"move":{"knock":{"card":{"rank":"K","suit":"♣"}}}})"))
          .ok());
  const json between =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  const json& gin = between["lastDeal"]["gin"];
  EXPECT_EQ(gin["ending"], "undercut");
  EXPECT_EQ(gin["knocker"], "player-2");
  EXPECT_EQ(gin["hands"][1]["deadwood"].dump(),
            R"([{"rank":"A","suit":"♣"},{"rank":"9","suit":"♠"}])");
  EXPECT_EQ(between["lastDeal"]["winner"], "player-1");
  EXPECT_EQ(between["lastDeal"]["points"], 35);
}

// The dealer can knock too: player-1, dealing, takes the 9♠ player-2
// passed and knocks on A♦ with 9-A♠ and 10-K♦ — gin, 25, and player-2's
// 10-A runs leave nothing to add.
TEST_F(RummyWireTest, TheDealerKnocksFromSeatZero) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  ChoosingTable(creator, joiner);
  ASSERT_TRUE(
      creator->Send(CommandFrame("rummy", R"({"move":{"chooseVariant":{"variant":"gin"}}})")).ok());
  for (int i = 0; i < 3; ++i) (void)NextFrame(*creator);
  for (int i = 0; i < 3; ++i) (void)NextFrame(*joiner);

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"pass":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  (void)EventPayload(NextFrame(*creator), "rummy");  // turnChanged
  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"drawDiscard":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  ASSERT_TRUE(
      creator->Send(CommandFrame("rummy", R"({"move":{"knock":{"card":{"rank":"A","suit":"♦"}}}})"))
          .ok());
  const json between =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  const json& gin = between["lastDeal"]["gin"];
  EXPECT_EQ(gin["ending"], "gin");
  EXPECT_EQ(gin["knocker"], "player-1");
  EXPECT_EQ(between["lastDeal"]["winner"], "player-1");
  EXPECT_EQ(between["lastDeal"]["points"], 25);
}

// Gin's moves are not rummy's: a pass or a knock at a 7-card table is
// refused by the table's game, as a meld at a gin table is.
TEST_F(RummyWireTest, GinMovesAtASevenCardTableAreRefused) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  DealtAndRead(creator, joiner);
  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"pass":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "commandRejected"),
            R"({"reason":"not a move in 7-card"})");
  ASSERT_TRUE(
      joiner->Send(CommandFrame("rummy", R"({"move":{"knock":{"card":{"rank":"A","suit":"♥"}}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "commandRejected"),
            R"({"reason":"not a move in 7-card"})");
}

// Taking the discard pile down to a card: player-2 draws the J♣ and throws
// the A♣ onto the J♦. player-1, holding Q♦ K♦, takes the pile down to the
// J♦ and melds it with them in the one move; the A♣ comes to hand.
TEST_F(RummyWireTest, TakingThePileDownMeldsItsDeepestCardAtOnce) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  DealtAndRead(creator, joiner);
  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"drawStock":{}}})")).ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*creator), "rummy");
  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame("rummy", R"({"move":{"discard":{"card":{"rank":"A","suit":"♣"}}}})"))
          .ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*joiner), "rummy");  // turnChanged
  const json mine =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  (void)EventPayload(NextFrame(*creator), "rummy");  // turnChanged
  EXPECT_EQ(mine["discardPile"].dump(), R"([{"rank":"J","suit":"♦"},{"rank":"A","suit":"♣"}])");

  // A plain draw takes the top alone.
  ASSERT_TRUE(creator
                  ->Send(CommandFrame(
                      "rummy", R"({"move":{"drawDiscard":{"card":{"rank":"J","suit":"♦"}}}})"))
                  .ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "commandRejected"),
            R"({"reason":"a draw takes the top card: take the pile down with takeDown"})");

  ASSERT_TRUE(creator
                  ->Send(CommandFrame(
                      "rummy", R"({"move":{"takeDown":{"card":{"rank":"J","suit":"♦"},)"
                               R"("cards":[{"rank":"Q","suit":"♦"},{"rank":"K","suit":"♦"}]}}})"))
                  .ok());
  const json took =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(took["discardPile"], json::array());
  EXPECT_EQ(took["melds"].back()["cards"].dump(),
            R"([{"rank":"J","suit":"♦"},{"rank":"Q","suit":"♦"},{"rank":"K","suit":"♦"}])");
  EXPECT_EQ(took["lastMove"]["move"], "takeDown");
  EXPECT_EQ(took["lastMove"]["cards"].dump(),
            R"([{"rank":"J","suit":"♦"},{"rank":"A","suit":"♣"}])");
  EXPECT_EQ(took["lastMove"]["meldIndex"], took["melds"].size() - 1);
  EXPECT_EQ(took["players"][0]["handCount"], 6);
  EXPECT_EQ(took["stage"], "play");
}

// A take-down melds cards from hand or lays off onto a meld, one or the
// other; a lay-off names a meld on the table, and the card named is in
// the pile.
TEST_F(RummyWireTest, ATakeDownNamesItsPlay) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  DealtAndRead(creator, joiner);
  const auto refused = [&](const std::string& take_down) {
    EXPECT_TRUE(
        joiner->Send(CommandFrame("rummy", R"({"move":{"takeDown":)" + take_down + "}}")).ok());
    return EventPayload(NextFrame(*joiner), "commandRejected");
  };
  EXPECT_EQ(refused(R"({"card":{"rank":"J","suit":"♦"}})"),
            R"({"reason":"a take-down melds cards from hand or lays off onto a meld"})");
  EXPECT_EQ(refused(R"({"card":{"rank":"J","suit":"♦"},"cards":[{"rank":"A","suit":"♥"}],)"
                    R"("meldIndex":0})"),
            R"({"reason":"a take-down melds cards from hand or lays off onto a meld"})");
  EXPECT_EQ(refused(R"({"card":{"rank":"J","suit":"♦"},"meldIndex":3})"),
            R"({"reason":"no such meld"})");
  EXPECT_EQ(refused(R"({"card":{"rank":"2","suit":"♣"},"meldIndex":0})"),
            R"({"reason":"that card is not in the discard pile"})");
  // The meld is the hand's; player-1 holds the Q♦ and K♦.
  EXPECT_EQ(refused(R"({"card":{"rank":"J","suit":"♦"},"cards":[{"rank":"Q","suit":"♦"},)"
                    R"({"rank":"K","suit":"♦"}]})"),
            R"({"reason":"not in your hand: Q♦"})");
}

TEST_F(RummyWireTest, StockDrawAndDiscardSpellings) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  DealtAndRead(creator, joiner);

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"drawStock":{}}})")).ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  const json drew = json::parse(EventPayload(NextFrame(*creator), "rummy"));
  // Nobody else sees a stock card: the move names none and no meld.
  EXPECT_EQ(drew["update"]["gameState"]["view"]["lastMove"].dump(),
            R"({"cards":[],"move":"drawStock","playerId":"player-2"})");

  ASSERT_TRUE(
      joiner
          ->Send(CommandFrame("rummy", R"({"move":{"discard":{"card":{"rank":"J","suit":"♣"}}}})"))
          .ok());
  (void)EventPayload(NextFrame(*joiner), "rummy");
  const json threw = json::parse(EventPayload(NextFrame(*creator), "rummy"));
  EXPECT_EQ(threw["update"]["gameState"]["view"]["lastMove"].dump(),
            R"({"cards":[{"rank":"J","suit":"♣"}],"move":"discard","playerId":"player-2"})");
  EXPECT_EQ(threw["update"]["gameState"]["view"]["discardTop"].dump(),
            R"({"rank":"J","suit":"♣"})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"turnChanged":{"playerId":"player-1"}}})");
}

}  // namespace
}  // namespace games_hub
