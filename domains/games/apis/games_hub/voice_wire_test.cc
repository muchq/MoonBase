// Wire-contract tests for a room's voice (#1590): the voice envelope's
// exact bytes both ways, driven through the generated server without the
// generated client, and the hub's part of it — voice is the session's
// room's, and leaving the room or closing the socket leaves voice with it.
// Voice's own rules are voice_test's. The harness is wire_test_fixture.h's.

#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "domains/games/apis/games_hub/wire_test_fixture.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

constexpr char kPlayPath[] = "/games/v2/play";
constexpr char kJoin[] = R"({"action":{"join":{}}})";

class VoiceWireTest : public HubWireFixture {
 protected:
  void SetUp() override {
    HubWireFixture::SetUp();
    moonbase::games::IceServer stun;
    stun.urls = {"stun:stun.example:3478"};
    golf_->SetIceServers({stun});
  }

  std::shared_ptr<opal::http::WebSocket> DialReady() {
    json session;
    return HubWireFixture::DialReady(kPlayPath, session);
  }

  // player-1 creates room-1 and player-2 joins it; every frame the room
  // layer owes either of them is read off.
  void TwoInARoom(std::shared_ptr<opal::http::WebSocket>& first,
                  std::shared_ptr<opal::http::WebSocket>& second) {
    first = DialReady();
    ASSERT_TRUE(first->Send(CommandFrame("createRoom", "{}")).ok());
    (void)EventPayload(NextFrame(*first), "roomState");
    second = DialReady();
    ASSERT_TRUE(second->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
    (void)EventPayload(NextFrame(*second), "roomState");
    (void)EventPayload(NextFrame(*second), "roomChatHistory");
    (void)EventPayload(NextFrame(*first), "roomState");
  }

  // Both in voice: player-1 first, so player-2's roster names player-1.
  void TwoInVoice(std::shared_ptr<opal::http::WebSocket>& first,
                  std::shared_ptr<opal::http::WebSocket>& second) {
    TwoInARoom(first, second);
    ASSERT_TRUE(first->Send(CommandFrame("voice", kJoin)).ok());
    (void)EventPayload(NextFrame(*first), "voice");
    ASSERT_TRUE(second->Send(CommandFrame("voice", kJoin)).ok());
    (void)EventPayload(NextFrame(*second), "voice");
    (void)EventPayload(NextFrame(*first), "voice");
  }
};

// Consumer: joining voice. The first in hears an empty roster carrying the
// ICE servers; the second hears the first on its roster, and the first
// hears the second join.
TEST_F(VoiceWireTest, JoinPinsRosterAndJoinedBytes) {
  std::shared_ptr<opal::http::WebSocket> first, second;
  TwoInARoom(first, second);
  ASSERT_TRUE(first->Send(CommandFrame("voice", kJoin)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"roster":{"epoch":1,"iceServers":[{"urls":["stun:stun.example:3478"]}],)"
            R"("members":[]}}})");
  ASSERT_TRUE(second->Send(CommandFrame("voice", kJoin)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "voice"),
            R"({"update":{"roster":{"epoch":2,"iceServers":[{"urls":["stun:stun.example:3478"]}],)"
            R"("members":[{"epoch":1,"playerId":"player-1"}]}}})");
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"joined":{"epoch":2,"playerId":"player-2"}}})");
}

// Consumer: the negotiation. An offer and a candidate reach the peer they
// name, as signals naming the sender, with their fields as sent — the
// candidate as a browser's toJSON() spells it, usernameFragment and all.
// player-1 joined voice first, so its epoch is 1 and player-2's is 2.
TEST_F(VoiceWireTest, SignalsPinTheirBytes) {
  std::shared_ptr<opal::http::WebSocket> first, second;
  TwoInVoice(first, second);
  ASSERT_TRUE(
      second
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                                       R"("description":{"type":"offer","sdp":"v=0"}}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"signal":{"description":{"sdp":"v=0","type":"offer"},)"
            R"("from":"player-2"}}})");
  ASSERT_TRUE(
      first
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-2","toEpoch":2,)"
                                       R"("candidate":{"candidate":"candidate:1",)"
                                       R"("sdpMid":"0","sdpMLineIndex":0,)"
                                       R"("usernameFragment":"ufrag"}}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "voice"),
            R"({"update":{"signal":{"candidate":{"candidate":"candidate:1","sdpMLineIndex":0,)"
            R"("sdpMid":"0","usernameFragment":"ufrag"},"from":"player-1"}}})");
}

// Consumer: a browser's RTCIceCandidate.toJSON() can carry null for
// sdpMid. It reads as absent — relayed, the field left out — so a client
// hands toJSON() over as it is.
TEST_F(VoiceWireTest, ANullCandidateFieldReadsAsAbsent) {
  std::shared_ptr<opal::http::WebSocket> first, second;
  TwoInVoice(first, second);
  ASSERT_TRUE(
      first
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-2","toEpoch":2,)"
                                       R"("candidate":{"candidate":"c","sdpMid":null,)"
                                       R"("sdpMLineIndex":0}}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "voice"),
            R"({"update":{"signal":{"candidate":{"candidate":"c","sdpMLineIndex":0},)"
            R"("from":"player-1"}}})");
}

// Consumer: the in-band refusals. Voice needs a room, and a signal to
// nobody is refused without saying whether they exist.
TEST_F(VoiceWireTest, RefusalsArriveAsCommandRejected) {
  auto alone = DialReady();
  ASSERT_TRUE(alone->Send(CommandFrame("voice", kJoin)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alone), "commandRejected"),
            R"({"reason":"join a room first"})");
  ASSERT_TRUE(alone->Send(CommandFrame("voice", R"({"action":{"leave":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alone), "commandRejected"), R"({"reason":"not in voice"})");
  ASSERT_TRUE(
      alone
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-9","toEpoch":1,)"
                                       R"("description":{"type":"offer","sdp":"v=0"}}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*alone), "commandRejected"),
            R"({"reason":"not in voice with that player"})");
}

// A signal over the model's @length bounds is refused in band by the
// generated decoder (opal ADR-0025); the peer never sees it and the session
// stays open.
TEST_F(VoiceWireTest, ASignalOverTheModelsBoundsIsRefusedAndTheSessionStays) {
  std::shared_ptr<opal::http::WebSocket> first, second;
  TwoInVoice(first, second);
  const std::string long_sdp(16 * 1024 + 1, 'x');
  ASSERT_TRUE(
      second
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                                       R"("description":{"type":"offer","sdp":")" +
                                           long_sdp + R"("}}}})"))
          .ok());
  const json refused = json::parse(EventPayload(NextFrame(*second), "commandRejected"));
  EXPECT_NE(refused["reason"].get<std::string>().find("sdp"), std::string::npos);
  EXPECT_NE(refused["reason"].get<std::string>().find("failed to satisfy constraint"),
            std::string::npos);
  const std::string long_mid(1025, 'x');
  ASSERT_TRUE(
      second
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                                       R"("candidate":{"candidate":"c","sdpMid":")" +
                                           long_mid + R"("}}}})"))
          .ok());
  (void)EventPayload(NextFrame(*second), "commandRejected");
  ASSERT_TRUE(
      second
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                                       R"("candidate":{"candidate":"c","usernameFragment":")" +
                                           long_mid + R"("}}}})"))
          .ok());
  (void)EventPayload(NextFrame(*second), "commandRejected");
  EXPECT_EQ(metrics_->CounterTotal("hub_rejections", {{"kind", "invalid"}}), 3);

  ASSERT_TRUE(
      second
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                                       R"("description":{"type":"offer","sdp":"v=0"}}}})"))
          .ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"signal":{"description":{"sdp":"v=0","type":"offer"},)"
            R"("from":"player-2"}}})");

  // The bound itself is allowed.
  const std::string at_sdp(16 * 1024, 'x');
  ASSERT_TRUE(
      second
          ->Send(CommandFrame("voice", R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                                       R"("description":{"type":"offer","sdp":")" +
                                           at_sdp + R"("}}}})"))
          .ok());
  const json relayed = json::parse(EventPayload(NextFrame(*first), "voice"));
  EXPECT_EQ(relayed["update"]["signal"]["description"]["sdp"].get<std::string>().size(),
            at_sdp.size());
}

// Voice is the room's: a deliberate leave, leaving the room, and a closed
// socket each take the member out, and whoever is left hears it.
TEST_F(VoiceWireTest, LeavingVoiceTheRoomOrTheSocketTellsTheRest) {
  std::shared_ptr<opal::http::WebSocket> first, second;
  TwoInVoice(first, second);
  ASSERT_TRUE(second->Send(CommandFrame("voice", R"({"action":{"leave":{}}})")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"left":{"playerId":"player-2"}}})");

  ASSERT_TRUE(second->Send(CommandFrame("voice", kJoin)).ok());
  (void)EventPayload(NextFrame(*second), "voice");
  (void)EventPayload(NextFrame(*first), "voice");
  ASSERT_TRUE(second->Send(CommandFrame("leaveRoom", "{}")).ok());
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"left":{"playerId":"player-2"}}})");
  (void)EventPayload(NextFrame(*first), "roomState");

  auto third = DialReady();
  ASSERT_TRUE(third->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*third), "roomState");
  (void)EventPayload(NextFrame(*third), "roomChatHistory");
  (void)EventPayload(NextFrame(*first), "roomState");
  ASSERT_TRUE(third->Send(CommandFrame("voice", kJoin)).ok());
  (void)EventPayload(NextFrame(*third), "voice");
  (void)EventPayload(NextFrame(*first), "voice");
  third->Close();
  EXPECT_EQ(EventPayload(NextFrame(*first), "voice"),
            R"({"update":{"left":{"playerId":"player-3"}}})");
}

// Signals draw from voice's own budget: two tokens and no refill, so the
// join and one signal spend them, the next signal is refused in band
// without reaching the peer, and the room's own commands still have theirs.
class VoiceRateLimitedTest : public VoiceWireTest {
 protected:
  RateLimits MakeRateLimits() override {
    RateLimits limits = WireRateLimits();
    limits.voice_burst = 2;
    limits.voice_refill_per_sec = 0;
    return limits;
  }
};

TEST_F(VoiceRateLimitedTest, ASignalFloodIsRefusedAfterVoicesOwnBurst) {
  std::shared_ptr<opal::http::WebSocket> first, second;
  TwoInVoice(first, second);
  constexpr char kOffer[] = R"({"action":{"signal":{"to":"player-1","toEpoch":1,)"
                            R"("description":{"type":"offer","sdp":"v=0"}}}})";
  ASSERT_TRUE(second->Send(CommandFrame("voice", kOffer)).ok());
  (void)EventPayload(NextFrame(*first), "voice");
  ASSERT_TRUE(second->Send(CommandFrame("voice", kOffer)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*second), "commandRejected"), R"({"reason":"slow down"})");
  ASSERT_TRUE(second->Send(CommandFrame("getRoomState", "{}")).ok());
  (void)EventPayload(NextFrame(*second), "roomState");
  // The peer's next frame is its own answer, not the refused signal.
  ASSERT_TRUE(first->Send(CommandFrame("getRoomState", "{}")).ok());
  (void)EventPayload(NextFrame(*first), "roomState");
  EXPECT_EQ(metrics_->CounterTotal("hub_rate_limited", {{"kind", "voice"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("hub_rate_limited", {{"kind", "command"}}), 0);
}

// Decoder refusals draw from the command bucket.
class DecoderRefusalLimitedTest : public VoiceWireTest {
 protected:
  RateLimits MakeRateLimits() override {
    RateLimits limits = WireRateLimits();
    limits.command_burst = 1;
    limits.command_refill_per_sec = 0;
    return limits;
  }
};

TEST_F(DecoderRefusalLimitedTest, AFloodOfOversizedEventsIsRateLimited) {
  auto alone = DialReady();
  const std::string oversized =
      R"({"action":{"signal":{"to":"player-9","toEpoch":1,"candidate":{"candidate":")" +
      std::string(1025, 'x') + R"("}}}})";
  ASSERT_TRUE(alone->Send(CommandFrame("voice", oversized)).ok());
  const json refused = json::parse(EventPayload(NextFrame(*alone), "commandRejected"));
  EXPECT_NE(refused["reason"].get<std::string>().find("failed to satisfy constraint"),
            std::string::npos);
  ASSERT_TRUE(alone->Send(CommandFrame("voice", oversized)).ok());
  EXPECT_EQ(EventPayload(NextFrame(*alone), "commandRejected"), R"({"reason":"slow down"})");
  EXPECT_EQ(metrics_->CounterTotal("hub_rate_limited", {{"kind", "command"}}), 1);
}

}  // namespace
}  // namespace games_hub
