// Wire-contract goldens for rummy (#245) on the room stream, the way
// castle_wire_test pins castle's: raw eventstream frames and exact payload
// bytes, because the typed-client suites regenerate both sides together
// and cannot see a rename. The pinned surface: the rummy command envelope
// ({"move":{...}} inside the `rummy` command) with every in-game move's
// spelling, the update envelope ({"update":{...}} inside the `rummy`
// event), the dealt RummyView's full key set from each chair, the view
// mid-turn (stage, takenDiscard, melds, lastMove), and gameEnded.
//
// The NoShuffleDealer deals one card a seat from the back of the pristine
// deck: player-1 holds A♠ A♦ K♠ K♦ Q♠ Q♦ J♠ J♦ 10♠ 10♦, player-2 the
// hearts and clubs, 9♠ is turned up and 9♥ tops the stock.

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

  // Two seats at a dealt rummy table; every frame up to the dealt views
  // read, the creator's dealt view returned.
  std::string DealtTable(std::shared_ptr<opal::http::WebSocket>& creator,
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
    (void)EventPayload(NextFrame(*joiner), "rummy");   // gameJoined
    (void)EventPayload(NextFrame(*creator), "rummy");  // gameState
    (void)EventPayload(NextFrame(*joiner), "roomState");
    (void)EventPayload(NextFrame(*creator), "roomState");
    EXPECT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"startGame":{}}})")).ok());
    EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"), R"({"update":{"gameStarted":{}}})");
    const std::string dealt = EventPayload(NextFrame(*creator), "rummy");
    (void)EventPayload(NextFrame(*creator), "roomState");
    (void)EventPayload(NextFrame(*joiner), "rummy");  // gameStarted
    return dealt;
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
            R"({"update":{"gameJoined":{"view":{"canDrawStock":false,"discardCount":0,)"
            R"("gameId":"GAME01","melds":[],"phase":"waiting","players":[{"hand":[],)"
            R"("handCount":0,"playerId":"player-1"}],"stockCount":0}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"rummy","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("geometry":{"plane":{}},"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","table":{"game":"rummy","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");
}

TEST_F(RummyWireTest, TheDealtViewFromEachChair) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  const std::string dealt = DealtTable(creator, joiner);
  EXPECT_EQ(
      dealt,
      R"({"update":{"gameState":{"view":{"canDrawStock":true,"currentPlayerId":"player-1",)"
      R"("discardCount":1,"discardTop":{"rank":"9","suit":"♠"},"gameId":"GAME01","melds":[],)"
      R"("phase":"playing","players":[{"hand":[{"rank":"A","suit":"♠"},{"rank":"A","suit":"♦"},)"
      R"({"rank":"K","suit":"♠"},{"rank":"K","suit":"♦"},{"rank":"Q","suit":"♠"},)"
      R"({"rank":"Q","suit":"♦"},{"rank":"J","suit":"♠"},{"rank":"J","suit":"♦"},)"
      R"({"rank":"10","suit":"♠"},{"rank":"10","suit":"♦"}],"handCount":10,)"
      R"("playerId":"player-1"},{"hand":[],"handCount":10,"playerId":"player-2"}],)"
      R"("stage":"draw","stockCount":31}}}})");
  const json view = json::parse(dealt)["update"]["gameState"]["view"];
  EXPECT_EQ(KeysOf(view),
            (std::set<std::string>{"canDrawStock", "currentPlayerId", "discardCount", "discardTop",
                                   "gameId", "melds", "phase", "players", "stage", "stockCount"}));
  EXPECT_EQ(KeysOf(view["players"][0]), (std::set<std::string>{"hand", "handCount", "playerId"}));
  // Absent optionals (takenDiscard, lastMove) are omitted keys, not nulls.
  EXPECT_FALSE(view.contains("takenDiscard"));
  EXPECT_FALSE(view.contains("lastMove"));

  // The other chair: its own faces, the creator's as a count.
  const json theirs =
      json::parse(EventPayload(NextFrame(*joiner), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(theirs["players"][0]["hand"], json::array());
  EXPECT_EQ(theirs["players"][0]["handCount"], 10);
  EXPECT_EQ(theirs["players"][1]["hand"][0], (json{{"rank", "A"}, {"suit", "♥"}}));
}

// Every in-game move's spelling, and what the table sees of each, down to
// the quickest win the deck allows and its ending.
TEST_F(RummyWireTest, TurnMovesPinTheirSpellingAndTheEndingBytes) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  (void)DealtTable(creator, joiner);
  (void)EventPayload(NextFrame(*joiner), "rummy");  // the joiner's dealt view
  (void)EventPayload(NextFrame(*joiner), "roomState");

  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"drawDiscard":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "rummy"),
            R"({"update":{"gameState":{"view":{"canDrawStock":true,"currentPlayerId":"player-1",)"
            R"("discardCount":0,"gameId":"GAME01","lastMove":{"cards":[{"rank":"9","suit":"♠"}],)"
            R"("move":"drawDiscard","playerId":"player-1"},"melds":[],"phase":"playing",)"
            R"("players":[{"hand":[],"handCount":11,"playerId":"player-1"},)"
            R"({"hand":[{"rank":"A","suit":"♥"},{"rank":"A","suit":"♣"},{"rank":"K","suit":"♥"},)"
            R"({"rank":"K","suit":"♣"},{"rank":"Q","suit":"♥"},{"rank":"Q","suit":"♣"},)"
            R"({"rank":"J","suit":"♥"},{"rank":"J","suit":"♣"},{"rank":"10","suit":"♥"},)"
            R"({"rank":"10","suit":"♣"}],"handCount":10,"playerId":"player-2"}],)"
            R"("stage":"play","stockCount":31,"takenDiscard":{"rank":"9","suit":"♠"}}}}})");

  ASSERT_TRUE(
      creator
          ->Send(CommandFrame(
              "rummy",
              R"({"move":{"meld":{"cards":[{"rank":"K","suit":"♠"},{"rank":"Q","suit":"♠"},)"
              R"({"rank":"J","suit":"♠"}]}}})"))
          .ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  const json melded = json::parse(EventPayload(NextFrame(*joiner), "rummy"));
  EXPECT_EQ(melded["update"]["gameState"]["view"]["melds"].dump(),
            R"([{"cards":[{"rank":"J","suit":"♠"},{"rank":"Q","suit":"♠"},)"
            R"({"rank":"K","suit":"♠"}],"owner":"player-1"}])");
  EXPECT_EQ(melded["update"]["gameState"]["view"]["lastMove"].dump(),
            R"({"cards":[{"rank":"J","suit":"♠"},{"rank":"Q","suit":"♠"},{"rank":"K","suit":"♠"}],)"
            R"("meldIndex":0,"move":"meld","playerId":"player-1"})");

  for (const char* card : {R"({"rank":"A","suit":"♠"})", R"({"rank":"10","suit":"♠"})",
                           R"({"rank":"9","suit":"♠"})"}) {
    ASSERT_TRUE(creator
                    ->Send(CommandFrame("rummy", std::string(R"({"move":{"layOff":{"card":)") +
                                                     card + R"(,"meldIndex":0}}})"))
                    .ok());
    (void)EventPayload(NextFrame(*creator), "rummy");
    const json laid = json::parse(EventPayload(NextFrame(*joiner), "rummy"));
    EXPECT_EQ(laid["update"]["gameState"]["view"]["lastMove"].dump(),
              std::string(R"({"cards":[)") + card +
                  R"(],"meldIndex":0,"move":"layOff","playerId":"player-1"})");
  }

  // The diamonds go down as one run and her hand is empty: the final views
  // show every hand, then the ending.
  ASSERT_TRUE(
      creator
          ->Send(CommandFrame(
              "rummy",
              R"({"move":{"meld":{"cards":[{"rank":"A","suit":"♦"},{"rank":"K","suit":"♦"},)"
              R"({"rank":"Q","suit":"♦"},{"rank":"J","suit":"♦"},{"rank":"10","suit":"♦"}]}}})"))
          .ok());
  const json final_view =
      json::parse(EventPayload(NextFrame(*creator), "rummy"))["update"]["gameState"]["view"];
  EXPECT_EQ(final_view["phase"], "ended");
  EXPECT_FALSE(final_view.contains("currentPlayerId"));
  EXPECT_FALSE(final_view.contains("stage"));
  EXPECT_FALSE(final_view.contains("takenDiscard"));
  EXPECT_EQ(final_view["canDrawStock"], false);
  EXPECT_EQ(final_view["players"][1]["hand"].size(), 10u);
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"gameEnded":{"points":82,"scores":[{"deadwood":0,"playerId":"player-1"},)"
            R"({"deadwood":82,"playerId":"player-2"}],"winner":"player-1"}}})");
}

// A table that broke up names no winner: the key is omitted, never null,
// which is what the UI reads as nobody having gone out.
TEST_F(RummyWireTest, AnAbandonedTableEndsWithNoWinnerKey) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  (void)DealtTable(creator, joiner);
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*joiner), "roomState");

  ASSERT_TRUE(joiner->Send(CommandFrame("rummy", R"({"move":{"leaveGame":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");  // the final view
  EXPECT_EQ(
      EventPayload(NextFrame(*creator), "rummy"),
      R"({"update":{"gameEnded":{"points":0,"scores":[{"deadwood":82,"playerId":"player-1"}]}}})");
}

TEST_F(RummyWireTest, StockDrawAndDiscardSpellings) {
  std::shared_ptr<opal::http::WebSocket> creator;
  std::shared_ptr<opal::http::WebSocket> joiner;
  (void)DealtTable(creator, joiner);
  (void)EventPayload(NextFrame(*joiner), "rummy");
  (void)EventPayload(NextFrame(*joiner), "roomState");

  ASSERT_TRUE(creator->Send(CommandFrame("rummy", R"({"move":{"drawStock":{}}})")).ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  const json drew = json::parse(EventPayload(NextFrame(*joiner), "rummy"));
  // Nobody else sees a stock card: the move names none and no meld.
  EXPECT_EQ(drew["update"]["gameState"]["view"]["lastMove"].dump(),
            R"({"cards":[],"move":"drawStock","playerId":"player-1"})");

  ASSERT_TRUE(
      creator
          ->Send(CommandFrame("rummy", R"({"move":{"discard":{"card":{"rank":"9","suit":"♥"}}}})"))
          .ok());
  (void)EventPayload(NextFrame(*creator), "rummy");
  const json threw = json::parse(EventPayload(NextFrame(*joiner), "rummy"));
  EXPECT_EQ(threw["update"]["gameState"]["view"]["lastMove"].dump(),
            R"({"cards":[{"rank":"9","suit":"♥"}],"move":"discard","playerId":"player-1"})");
  EXPECT_EQ(threw["update"]["gameState"]["view"]["discardTop"].dump(),
            R"({"rank":"9","suit":"♥"})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "rummy"),
            R"({"update":{"turnChanged":{"playerId":"player-2"}}})");
}

}  // namespace
}  // namespace games_hub
