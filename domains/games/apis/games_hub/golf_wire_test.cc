// Beyoncé Rule wire-contract tests: golden fixtures pinning the raw HTTP
// bytes and eventstream frames the golf WEB CLIENT (the browser client on
// ADR-0018's JSON-text wire) depends on. The typed-client e2e suites
// cannot catch a wire rename — both sides regenerate together — so every
// assertion here is on raw strings: paths, status codes, JSON key names,
// envelope headers (:message-type / :event-type / :exception-type /
// :content-type), and exact payload bytes (smithy::json::Encode is
// compact with sorted keys, and NoShuffleDealer + SequentialIdGenerator
// pin the values too).
//
// The pinned surface, exactly: the two routes; the mint and resume
// bodies; sessionReady fresh (no roomId) and resumed (roomId present);
// roomState; roomLeft; commandRejected; the joiner's roomState-then-
// roomChatHistory order (history sent even when empty); roomChat's
// server-assigned messageId and sentAtUnixMillis; the golf command
// envelope ({"move":{...}}) and update envelope ({"update":{...}})
// through createGame → gameCreated/gameJoined, joinGame →
// gameJoined/gameState, startGame → gameStarted plus the dealt opening
// GameView (the full key set, card rank/suit spelling included); and the
// two terminal exception frames (Unauthenticated, SeatConflict). Updates
// this suite does not drive (peek/draw/swap/knock, turnChanged,
// gameEnded) share GameView's pinned shape but keep their byte-level
// coverage in hub_e2e_test's typed assertions only.
//
// The harness is wire_test_fixture.h's: GolfHub behind the generated
// GamesHubServer, unary requests through Loopback, streams through
// StreamRouter()->ServeSession() over an InMemoryWebSocketPair whose near
// end this test holds and drives with hand-built frames. Non-Beast, so it
// runs with no sandbox setup at all.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "smithy/http/message.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

// The golf stream route; renaming it strands every deployed web client.
constexpr char kPlayPath[] = "/games/v2/golf/play";

class GolfWireTest : public HubWireFixture {
 protected:
  std::shared_ptr<smithy::http::WebSocket> DialPlay(const std::string& query) {
    return DialStream(kPlayPath, query);
  }
  std::shared_ptr<smithy::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }
};

// Consumer: the golf web client's session mint. It reads exactly these
// three camelCase keys from the response body; the byte-level prefix pins
// key order and spelling so a codegen change that renames or re-nests any
// of them fails here, not in a browser.
TEST_F(GolfWireTest, SessionMintPinsRawResponseBody) {
  const auto response = PostSession("{}");
  ASSERT_EQ(response.status, 200) << response.body;
  EXPECT_EQ(response.headers.Get("content-type").value_or(""), "application/json");

  // Compact JSON, sorted keys: playerId, resumeToken, ticket. The token
  // and ticket values are random, so the pin is prefix + seams + suffix.
  EXPECT_TRUE(response.body.starts_with(R"({"playerId":"player-1","resumeToken":")"))
      << response.body;
  EXPECT_NE(response.body.find(R"(","ticket":")"), std::string::npos) << response.body;
  EXPECT_TRUE(response.body.ends_with(R"("})")) << response.body;

  const json body = json::parse(response.body);
  std::set<std::string> keys;
  for (const auto& [key, value] : body.items()) keys.insert(key);
  EXPECT_EQ(keys, (std::set<std::string>{"playerId", "resumeToken", "ticket"}));
  EXPECT_FALSE(body["ticket"].get<std::string>().empty());
  EXPECT_FALSE(body["resumeToken"].get<std::string>().empty());
}

// Consumer: the golf web client's reconnect flow. It stores resumeToken
// and replays it as {"resumeToken": ...}; the hub must return the same
// playerId and echo the same token (the long-lived credential must not
// churn), while minting a fresh single-use ticket.
TEST_F(GolfWireTest, ResumeTokenRoundTripsThroughTheRawWire) {
  const json first = MintSession();

  const std::string resume_body =
      std::string(R"({"resumeToken":")") + first["resumeToken"].get<std::string>() + R"("})";
  const auto response = PostSession(resume_body);
  ASSERT_EQ(response.status, 200) << response.body;

  const json resumed = json::parse(response.body);
  EXPECT_EQ(resumed["playerId"], first["playerId"]);
  EXPECT_EQ(resumed["resumeToken"], first["resumeToken"]);
  EXPECT_NE(resumed["ticket"], first["ticket"]);
}

// Consumer: the golf web client's Play dial error handling. On this wire
// an unspendable ticket is refused AFTER the upgrade as one terminal
// exception frame, then a clean close — exactly what the browser's
// event-stream decoder must surface as its dial failure. (Production
// main.cc additionally refuses obviously-bad tickets pre-101 with a 401
// gate; a ticket that expires between that peek and the handler's spend
// still lands here, so this frame is the stream's own refusal contract.)
TEST_F(GolfWireTest, InvalidTicketRefusesWithTerminalUnauthenticatedFrame) {
  auto socket = DialPlay("?ticket=bogus");

  const auto frame = NextFrame(*socket);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(HeaderText(*frame, ":message-type"), "exception");
  EXPECT_EQ(HeaderText(*frame, ":exception-type"), "Unauthenticated");
  EXPECT_EQ(HeaderText(*frame, ":content-type"), "application/json");
  EXPECT_EQ(frame->headers.size(), 3u);
  EXPECT_EQ(frame->payload.ToString(), R"({"message":"ticket expired or already spent"})");

  // The exception is terminal: the server closes and nothing follows.
  auto closed = socket->Receive(kWireReceiveBudget);
  ASSERT_TRUE(closed.ok()) << closed.error().message();
  EXPECT_FALSE(closed->has_value()) << "expected a clean close after the exception frame";
}

// Consumer: the golf web client's whole happy path, raw end to end: mint
// a session over HTTP, dial Play with the ticket, read sessionReady, send
// createRoom, read roomState. Pins the envelope headers and the exact
// payload bytes (key names AND values are deterministic under the
// sequential id generator) for both server events, plus the command
// encoding the client sends.
TEST_F(GolfWireTest, ValidSessionStreamsSessionReadyAndRoomStateFrames) {
  const json session = MintSession();
  auto socket = DialPlay("?ticket=" + session["ticket"].get<std::string>());

  // Frame 1: sessionReady, addressed to the freshly minted player. A
  // fresh (non-resumed) seat has no roomId member at all.
  const auto ready = NextFrame(*socket);
  ASSERT_TRUE(ready.has_value());
  EXPECT_EQ(HeaderText(*ready, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*ready, ":event-type"), "sessionReady");
  EXPECT_EQ(HeaderText(*ready, ":content-type"), "application/json");
  EXPECT_EQ(ready->payload.ToString(), R"({"playerId":"player-1","resumed":false})");

  // The createRoom command, framed exactly as the generated client (and
  // the browser wire) encode it.
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());

  // Frame 2: the creator's roomState snapshot — the room id, the member
  // roster with per-player lobby stats, and the (empty) games list.
  const auto room = NextFrame(*socket);
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(HeaderText(*room, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*room, ":event-type"), "roomState");
  EXPECT_EQ(HeaderText(*room, ":content-type"), "application/json");
  EXPECT_EQ(room->payload.ToString(),
            R"({"games":[],"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","totalScore":0}],"roomId":"room-1"})");
}

// Consumer: the golf web client's in-band error toast. A syntactically
// valid command the hub refuses (joining a room that does not exist)
// comes back as a commandRejected EVENT — not an exception, the stream
// survives — whose payload carries the one "reason" key the UI renders.
TEST_F(GolfWireTest, SemanticallyInvalidCommandYieldsCommandRejectedEvent) {
  const json session = MintSession();
  auto socket = DialPlay("?ticket=" + session["ticket"].get<std::string>());
  const auto ready = NextFrame(*socket);
  ASSERT_TRUE(ready.has_value());
  ASSERT_EQ(HeaderText(*ready, ":event-type"), "sessionReady");

  ASSERT_TRUE(socket->Send(CommandFrame("joinRoom", R"({"roomId":"ZZZZ99"})")).ok());

  const auto rejected = NextFrame(*socket);
  ASSERT_TRUE(rejected.has_value());
  EXPECT_EQ(HeaderText(*rejected, ":message-type"), "event");
  EXPECT_EQ(HeaderText(*rejected, ":event-type"), "commandRejected");
  EXPECT_EQ(HeaderText(*rejected, ":content-type"), "application/json");
  EXPECT_EQ(rejected->payload.ToString(), R"({"reason":"room unavailable or already in a room"})");

  // The stream survived the rejection: the next command still lands.
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());
  const auto room = NextFrame(*socket);
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(HeaderText(*room, ":event-type"), "roomState");
}

// Consumer: the golf web client's game UI, whose whole vocabulary is the
// golf envelope — {"move":{...}} up inside the `golf` command,
// {"update":{...}} down inside the `golf` event. Under NoShuffleDealer +
// SequentialIdGenerator every byte is deterministic, so the goldens pin
// the GolfUpdate member names (gameCreated, gameJoined, gameState,
// gameStarted) and the full GameView key set: the waiting view on
// create/join, and the dealt opening view — currentPlayerId, discardTop,
// and the card rank/suit spelling — on start.
TEST_F(GolfWireTest, GameFlowPinsGolfCommandAndUpdatePayloadBytes) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");

  // createGame, framed exactly as the browser mints it. The creator hears
  // the room-wide announcement, then their own seat (a gameJoined with the
  // waiting-phase view: four face-down slots, no optional members), then
  // the lobby list gaining the game and the creator's seat at it. A
  // member at no table carries no seat key at all (the goldens above).
  ASSERT_TRUE(creator->Send(CommandFrame("golf", R"({"move":{"createGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"),
            R"({"update":{"gameCreated":{"createdBy":"player-1","gameId":"GAME01"}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"),
            R"({"update":{"gameJoined":{"view":{"allPlayersPeeked":false,"discardCount":0,)"
            R"("drawPileCount":0,"gameId":"GAME01","phase":"waiting","players":[)"
            R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-1",)"
            R"("revealedIndexes":[]}]}}}})");
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"golf","gameId":"GAME01","playerCount":1,"status":"waiting"}],)"
            R"("players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","seat":{"game":"golf","gameId":"GAME01"},)"
            R"("totalScore":0}],"roomId":"room-1"})");

  // A second session joins the room (that admission sequence is pinned in
  // the chat-ordering test) and then the game.
  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*joiner), "roomChatHistory");
  (void)EventPayload(NextFrame(*creator), "roomState");

  ASSERT_TRUE(
      joiner->Send(CommandFrame("golf", R"({"move":{"joinGame":{"gameId":"GAME01"}}})")).ok());
  const std::string waiting_pair =
      R"("view":{"allPlayersPeeked":false,"discardCount":0,)"
      R"("drawPileCount":0,"gameId":"GAME01","phase":"waiting","players":[)"
      R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-1","revealedIndexes":[]},)"
      R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-2","revealedIndexes":[]}]})";
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "golf"),
            R"({"update":{"gameJoined":{)" + waiting_pair + R"(}}})");
  // The sitting player hears the seat fill as a gameState of the same view.
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"),
            R"({"update":{"gameState":{)" + waiting_pair + R"(}}})");
  (void)EventPayload(NextFrame(*joiner), "roomState");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // startGame: the bare gameStarted marker, then the dealt opening view —
  // the frame that carries the rest of GameView's keys. Both hands stay
  // face down even to their owners; only the seeded discard shows a face.
  ASSERT_TRUE(creator->Send(CommandFrame("golf", R"({"move":{"startGame":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*creator), "golf"), R"({"update":{"gameStarted":{}}})");
  const auto opening = NextFrame(*creator);
  const std::string opening_payload = EventPayload(opening, "golf");
  EXPECT_EQ(opening_payload,
            R"({"update":{"gameState":{"view":{"allPlayersPeeked":false,)"
            R"("currentPlayerId":"player-1","discardCount":1,)"
            R"("discardTop":{"rank":"Q","suit":"♠"},"drawPileCount":43,"gameId":"GAME01",)"
            R"("phase":"playing","players":[)"
            R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-1",)"
            R"("revealedIndexes":[]},)"
            R"({"cards":[{},{},{},{}],"hasPeeked":false,"playerId":"player-2",)"
            R"("revealedIndexes":[]}]}}}})");
  // The same pin, spelled as the key set the UI destructures — so a
  // failure names the missing/renamed member even if bytes drift for an
  // unrelated reason first.
  const json view = json::parse(opening_payload)["update"]["gameState"]["view"];
  EXPECT_EQ(KeysOf(view),
            (std::set<std::string>{"allPlayersPeeked", "currentPlayerId", "discardCount",
                                   "discardTop", "drawPileCount", "gameId", "phase", "players"}));
  EXPECT_EQ(KeysOf(view["players"][0]),
            (std::set<std::string>{"cards", "hasPeeked", "playerId", "revealedIndexes"}));
  EXPECT_EQ(KeysOf(view["discardTop"]), (std::set<std::string>{"rank", "suit"}));

  // The lobby list flips to playing for everyone in the room, and both
  // members read as seated at it (#1490): the seat key is the lobby's.
  EXPECT_EQ(EventPayload(NextFrame(*creator), "roomState"),
            R"({"games":[{"game":"golf","gameId":"GAME01","playerCount":2,"status":"playing"}],)"
            R"("players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","seat":{"game":"golf","gameId":"GAME01"},)"
            R"("totalScore":0},)"
            R"({"connected":true,"gamesPlayed":0,"gamesWon":0,"playerId":"player-2",)"
            R"("seat":{"game":"golf","gameId":"GAME01"},"totalScore":0}],"roomId":"room-1"})");
}

// Consumer: the golf web client's room-entry and chat rendering. The
// joiner's admission order is part of the contract: roomState first, then
// exactly one roomChatHistory — sent even when the room has no chat, so
// the client hears "history loaded, and it is empty" instead of inferring
// emptiness from silence (golf_hub.cc's documented signal). Live chat
// then carries the server's messageId and clock: the id is the golden
// byte, the timestamp a seam.
TEST_F(GolfWireTest, JoinerHearsRoomStateThenEmptyChatHistoryAndChatCarriesServerIds) {
  json creator_session;
  auto creator = DialReady(creator_session);
  ASSERT_TRUE(creator->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*creator), "roomState");

  json joiner_session;
  auto joiner = DialReady(joiner_session);
  ASSERT_TRUE(joiner->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());

  EXPECT_EQ(EventPayload(NextFrame(*joiner), "roomState"),
            R"({"games":[],"players":[{"connected":true,"gamesPlayed":0,"gamesWon":0,)"
            R"("playerId":"player-1","totalScore":0},)"
            R"({"connected":true,"gamesPlayed":0,"gamesWon":0,"playerId":"player-2",)"
            R"("totalScore":0}],"roomId":"room-1"})");
  EXPECT_EQ(EventPayload(NextFrame(*joiner), "roomChatHistory"), R"({"messages":[]})");
  (void)EventPayload(NextFrame(*creator), "roomState");

  // One chat message: stored before it is echoed, so both members receive
  // the identical frame with the server-assigned id (the first in a fresh
  // store is 1) and the server's clock in sentAtUnixMillis.
  ASSERT_TRUE(creator->Send(CommandFrame("chat", R"({"text":"gl"})")).ok());
  const std::string to_joiner = EventPayload(NextFrame(*joiner), "roomChat");
  const std::string echo = EventPayload(NextFrame(*creator), "roomChat");
  EXPECT_EQ(to_joiner, echo) << "one message, one set of bytes for every member";
  EXPECT_TRUE(to_joiner.starts_with(R"({"messageId":1,"playerId":"player-1","sentAtUnixMillis":)"))
      << to_joiner;
  EXPECT_TRUE(to_joiner.ends_with(R"(,"text":"gl"})")) << to_joiner;
  const json message = json::parse(to_joiner);
  EXPECT_EQ(KeysOf(message),
            (std::set<std::string>{"messageId", "playerId", "sentAtUnixMillis", "text"}));
  EXPECT_GT(message["sentAtUnixMillis"].get<int64_t>(), 0);
}

// Consumer: the golf web client's reconnect flow, this time all the way
// down the stream. After a drop the client re-POSTs its resumeToken,
// redials with the fresh ticket, and steers on the resumed sessionReady:
// resumed true AND the roomId member PRESENT — that key's arrival is what
// tells the UI to restore the room screen instead of the lobby.
TEST_F(GolfWireTest, ResumedSessionReadyCarriesRoomIdOnTheWire) {
  json session;
  auto socket = DialReady(session);
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*socket), "roomState");

  // The drop: a clean close parks the seat for the grace window.
  socket->Close();

  // The reconnect: same token, same player, fresh single-use ticket.
  const std::string resume_body =
      std::string(R"({"resumeToken":")") + session["resumeToken"].get<std::string>() + R"("})";
  const auto response = PostSession(resume_body);
  ASSERT_EQ(response.status, 200) << response.body;
  const json resumed = json::parse(response.body);
  ASSERT_EQ(resumed["playerId"], "player-1");

  // The redial admits as a resume (ResumeOrAdd retries past the close
  // still settling): sessionReady flips resumed and names the room.
  auto redialed = DialPlay("?ticket=" + resumed["ticket"].get<std::string>());
  const auto ready = NextFrame(*redialed);
  EXPECT_EQ(EventPayload(ready, "sessionReady"),
            R"({"playerId":"player-1","resumed":true,"roomId":"room-1"})");

  // The resumed stream then gets the room snapshot it missed and the
  // (empty) chat replay, in the same order a join pins.
  (void)EventPayload(NextFrame(*redialed), "roomState");
  EXPECT_EQ(EventPayload(NextFrame(*redialed), "roomChatHistory"), R"({"messages":[]})");
}

// Consumer: the golf web client's deliberate exit. Only the explicit
// leaveRoom command carries leave intent (a close parks the seat), and
// the ack the UI keys its navigation on is the roomLeft event naming the
// departed room.
TEST_F(GolfWireTest, LeaveRoomAcksWithRoomLeftFrame) {
  json session;
  auto socket = DialReady(session);
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*socket), "roomState");

  ASSERT_TRUE(socket->Send(CommandFrame("leaveRoom", "{}")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*socket), "roomLeft"), R"({"roomId":"room-1"})");
}

// Consumer: the golf web client's second-tab handling. A fresh ticket for
// a player whose first wire is still healthy is refused after the upgrade
// as one terminal SeatConflict exception frame, then a clean close — the
// same three-header envelope as Unauthenticated, so one decoder handles
// every dial failure. (The payload carries only "message"; the "__type"
// member belongs to the unary error envelope, not stream exceptions.)
TEST_F(GolfWireTest, SecondDialWhileSeatIsLiveRefusesWithSeatConflictFrame) {
  json session;
  auto socket = DialReady(session);

  const std::string resume_body =
      std::string(R"({"resumeToken":")") + session["resumeToken"].get<std::string>() + R"("})";
  const auto response = PostSession(resume_body);
  ASSERT_EQ(response.status, 200) << response.body;
  const json second = json::parse(response.body);

  auto conflicted = DialPlay("?ticket=" + second["ticket"].get<std::string>());
  const auto frame = NextFrame(*conflicted);
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(HeaderText(*frame, ":message-type"), "exception");
  EXPECT_EQ(HeaderText(*frame, ":exception-type"), "SeatConflict");
  EXPECT_EQ(HeaderText(*frame, ":content-type"), "application/json");
  EXPECT_EQ(frame->headers.size(), 3u);
  EXPECT_EQ(frame->payload.ToString(), R"({"message":"player already has a live connection"})");

  // Terminal for the second dial only: it closes cleanly, and the first
  // wire is still live — the next command lands.
  auto closed = conflicted->Receive(kWireReceiveBudget);
  ASSERT_TRUE(closed.ok()) << closed.error().message();
  EXPECT_FALSE(closed->has_value()) << "expected a clean close after the exception frame";
  ASSERT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());
  (void)EventPayload(NextFrame(*socket), "roomState");
}

}  // namespace
}  // namespace games_hub
