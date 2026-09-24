// A room's voice on the Voice itself (#1590): who is in it, what a join,
// leave, or signal stages and to whom, and what each is refused for. The
// hub's wiring of it (the room, the socket, the budget) is voice_wire_test's.

#include "domains/games/apis/games_hub/voice.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/hub_metrics.h"

namespace games_hub {
namespace {

using moonbase::games::IceCandidate;
using moonbase::games::SendSignal;
using moonbase::games::SessionDescription;

std::string Reason(const std::optional<Refusal>& refusal) {
  return refusal.has_value() ? refusal->reason : "<admitted>";
}

// "<to>:<case>" per delivery, in staged order.
std::vector<std::string> Staged(const Voice::Deliveries& out) {
  std::vector<std::string> staged;
  for (const auto& delivery : out) {
    staged.push_back(delivery.to + ":" + std::string(delivery.update.case_name()));
  }
  return staged;
}

// The epoch a joiner was given: the last roster staged to it.
std::int64_t EpochOf(const std::string& player_id, const Voice::Deliveries& out) {
  for (auto it = out.rbegin(); it != out.rend(); ++it) {
    if (it->to == player_id && it->update.as_roster_or_null() != nullptr) {
      return it->update.as_roster().epoch;
    }
  }
  ADD_FAILURE() << "no roster for " << player_id;
  return -1;
}

SendSignal Offer(std::string to, std::int64_t epoch, std::string sdp = "v=0") {
  SendSignal signal;
  signal.to = std::move(to);
  signal.toEpoch = epoch;
  SessionDescription description;
  description.type = "offer";
  description.sdp = std::move(sdp);
  signal.description = std::move(description);
  return signal;
}

SendSignal Candidate(std::string to, std::int64_t epoch, std::string candidate) {
  SendSignal signal;
  signal.to = std::move(to);
  signal.toEpoch = epoch;
  IceCandidate ice;
  ice.candidate = std::move(candidate);
  ice.sdpMid = "0";
  ice.sdpMLineIndex = 0;
  signal.candidate = std::move(ice);
  return signal;
}

// Two in R1's voice, alice first; their epochs.
struct Pair {
  std::int64_t alice;
  std::int64_t bob;
};

Pair AliceAndBob(Voice& voice) {
  Voice::Deliveries out;
  EXPECT_FALSE(voice.Join("alice", "R1", out).has_value());
  EXPECT_FALSE(voice.Join("bob", "R1", out).has_value());
  return {EpochOf("alice", out), EpochOf("bob", out)};
}

TEST(VoiceTest, AJoinerHearsWhoIsAlreadyInAndTheyHearTheJoiner) {
  Voice voice;
  moonbase::games::IceServer stun;
  stun.urls = {"stun:stun.example:3478"};
  voice.SetIceServers({stun});
  Voice::Deliveries out;

  ASSERT_EQ(Reason(voice.Join("alice", "R1", out)), "<admitted>");
  ASSERT_EQ(Staged(out), std::vector<std::string>{"alice:roster"});
  EXPECT_TRUE(out[0].update.as_roster().members.empty());
  ASSERT_EQ(out[0].update.as_roster().iceServers.size(), 1u);
  EXPECT_EQ(out[0].update.as_roster().iceServers[0].urls[0], "stun:stun.example:3478");
  const std::int64_t alice = EpochOf("alice", out);

  out.clear();
  ASSERT_EQ(Reason(voice.Join("bob", "R1", out)), "<admitted>");
  EXPECT_EQ(Staged(out), (std::vector<std::string>{"bob:roster", "alice:joined"}));
  const auto& members = out[0].update.as_roster().members;
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0].playerId, "alice");
  EXPECT_EQ(members[0].epoch, alice);
  EXPECT_EQ(out[1].update.as_joined().playerId, "bob");
  EXPECT_EQ(out[1].update.as_joined().epoch, EpochOf("bob", out));
  EXPECT_NE(EpochOf("bob", out), alice);
}

TEST(VoiceTest, WithNoIceServersSetARosterNamesNone) {
  Voice voice;
  Voice::Deliveries out;
  ASSERT_FALSE(voice.Join("alice", "R1", out).has_value());
  EXPECT_TRUE(out[0].update.as_roster().iceServers.empty());
}

TEST(VoiceTest, VoiceStaysInItsRoom) {
  Voice voice;
  Voice::Deliveries out;
  ASSERT_FALSE(voice.Join("alice", "R1", out).has_value());
  out.clear();
  ASSERT_FALSE(voice.Join("carol", "R2", out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"carol:roster"});
  EXPECT_TRUE(out[0].update.as_roster().members.empty());
  out.clear();
  ASSERT_TRUE(voice.Leave("carol", out));
  EXPECT_TRUE(out.empty());
}

TEST(VoiceTest, AJoinIsRefusedWhileInVoiceOrWhenVoiceIsFull) {
  Voice voice;
  Voice::Deliveries out;
  for (std::size_t i = 0; i < Voice::kCapacity; ++i) {
    ASSERT_FALSE(voice.Join("p" + std::to_string(i), "R1", out).has_value());
  }
  out.clear();
  EXPECT_EQ(Reason(voice.Join("p0", "R1", out)), "already in voice");
  const auto full = voice.Join("late", "R1", out);
  EXPECT_EQ(Reason(full), "voice is full");
  EXPECT_EQ(full->kind, RejectKind::kState);
  EXPECT_TRUE(out.empty());
  // Another room's voice has its own room.
  EXPECT_FALSE(voice.Join("late", "R2", out).has_value());
  // A leave frees a place.
  out.clear();
  ASSERT_TRUE(voice.Leave("late", out));
  ASSERT_TRUE(voice.Leave("p3", out));
  EXPECT_FALSE(voice.Join("late", "R1", out).has_value());
}

TEST(VoiceTest, ALeaveTellsTheRestAndIsFalseOutsideVoice) {
  Voice voice;
  AliceAndBob(voice);
  Voice::Deliveries out;
  ASSERT_TRUE(voice.Leave("bob", out));
  EXPECT_EQ(Staged(out), std::vector<std::string>{"alice:left"});
  EXPECT_EQ(out[0].update.as_left().playerId, "bob");
  out.clear();
  EXPECT_FALSE(voice.Leave("bob", out));
  EXPECT_FALSE(voice.Leave("nobody", out));
  EXPECT_TRUE(out.empty());
}

TEST(VoiceTest, ASignalReachesItsPeerNamingTheSender) {
  Voice voice;
  const Pair epochs = AliceAndBob(voice);
  Voice::Deliveries out;

  ASSERT_EQ(Reason(voice.Signal("bob", Offer("alice", epochs.alice, "v=0 offer"), out)),
            "<admitted>");
  ASSERT_EQ(Staged(out), std::vector<std::string>{"alice:signal"});
  const auto& offer = out[0].update.as_signal();
  EXPECT_EQ(offer.from, "bob");
  ASSERT_TRUE(offer.description.has_value());
  EXPECT_EQ(offer.description->type, "offer");
  EXPECT_EQ(offer.description->sdp, "v=0 offer");
  EXPECT_FALSE(offer.candidate.has_value());

  out.clear();
  SendSignal candidate = Candidate("bob", epochs.bob, "candidate:1 1 udp 1 10.0.0.1 9 typ host");
  candidate.candidate->usernameFragment = "ufrag";
  ASSERT_FALSE(voice.Signal("alice", candidate, out).has_value());
  ASSERT_EQ(Staged(out), std::vector<std::string>{"bob:signal"});
  ASSERT_TRUE(out[0].update.as_signal().candidate.has_value());
  EXPECT_EQ(out[0].update.as_signal().candidate->sdpMLineIndex, 0);
  EXPECT_EQ(out[0].update.as_signal().candidate->usernameFragment, "ufrag");

  // An empty candidate is the end of a peer's candidates, and relays.
  out.clear();
  EXPECT_FALSE(voice.Signal("alice", Candidate("bob", epochs.bob, ""), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"bob:signal"});
}

// Whether the peer exists, is in another room's voice, or is in no voice
// at all, the sender hears the same thing: a signal is not a way to ask
// who is online.
TEST(VoiceTest, ASignalOutsideOneVoiceIsRefusedTheSameWayWhoeverThePeerIs) {
  Voice voice;
  Voice::Deliveries out;
  ASSERT_FALSE(voice.Join("alice", "R1", out).has_value());
  const std::int64_t alice = EpochOf("alice", out);
  ASSERT_FALSE(voice.Join("carol", "R2", out).has_value());
  const std::int64_t carol = EpochOf("carol", out);
  out.clear();
  const std::string refused = "not in voice with that player";
  EXPECT_EQ(Reason(voice.Signal("alice", Offer("carol", carol), out)), refused);
  EXPECT_EQ(Reason(voice.Signal("alice", Offer("nobody", 1), out)), refused);
  EXPECT_EQ(Reason(voice.Signal("alice", Offer("alice", alice), out)), refused);
  EXPECT_EQ(Reason(voice.Signal("dave", Offer("alice", alice), out)), refused);
  EXPECT_EQ(voice.Signal("dave", Offer("alice", alice), out)->kind, RejectKind::kState);
  EXPECT_TRUE(out.empty());
}

TEST(VoiceTest, ASignalCarriesExactlyOneWellFormedPart) {
  Voice voice;
  const std::int64_t bob = AliceAndBob(voice).bob;
  Voice::Deliveries out;

  SendSignal neither;
  neither.to = "bob";
  neither.toEpoch = bob;
  const auto empty = voice.Signal("alice", neither, out);
  EXPECT_EQ(Reason(empty), "a signal carries a description or a candidate");
  EXPECT_EQ(empty->kind, RejectKind::kInvalid);

  SendSignal both = Offer("bob", bob);
  both.candidate = Candidate("bob", bob, "c").candidate;
  EXPECT_EQ(Reason(voice.Signal("alice", both, out)),
            "a signal carries a description or a candidate");

  SendSignal rollback = Offer("bob", bob);
  rollback.description->type = "rollback";
  EXPECT_EQ(Reason(voice.Signal("alice", rollback, out)), "a description is an offer or an answer");

  const std::string long_sdp(Voice::kMaxSdpBytes + 1, 'x');
  const std::string long_candidate(Voice::kMaxCandidateBytes + 1, 'x');
  EXPECT_EQ(Reason(voice.Signal("alice", Offer("bob", bob, long_sdp), out)), "sdp is too long");
  EXPECT_EQ(Reason(voice.Signal("alice", Candidate("bob", bob, long_candidate), out)),
            "candidate is too long");
  SendSignal long_mid = Candidate("bob", bob, "c");
  long_mid.candidate->sdpMid = std::string(Voice::kMaxCandidateBytes + 1, 'x');
  EXPECT_EQ(Reason(voice.Signal("alice", long_mid, out)), "candidate is too long");
  EXPECT_TRUE(out.empty());

  // The limits themselves are allowed.
  const std::string at_sdp(Voice::kMaxSdpBytes, 'x');
  const std::string at_candidate(Voice::kMaxCandidateBytes, 'x');
  EXPECT_FALSE(voice.Signal("alice", Offer("bob", bob, at_sdp), out).has_value());
  EXPECT_FALSE(voice.Signal("alice", Candidate("bob", bob, at_candidate), out).has_value());
  EXPECT_EQ(out.size(), 2u);
}

TEST(VoiceTest, SomeoneWhoLeftIsNoLongerSignalled) {
  Voice voice;
  const std::int64_t bob = AliceAndBob(voice).bob;
  Voice::Deliveries out;
  ASSERT_TRUE(voice.Leave("bob", out));
  out.clear();
  EXPECT_EQ(Reason(voice.Signal("alice", Offer("bob", bob), out)), "not in voice with that player");
  EXPECT_TRUE(out.empty());
}

// An answer to an offer from before someone left and came back belongs to
// a connection that is gone: it is refused, not applied to the new one.
TEST(VoiceTest, ASignalForAnEarlierJoinIsRefused) {
  Voice voice;
  const std::int64_t first = AliceAndBob(voice).alice;
  Voice::Deliveries out;
  ASSERT_TRUE(voice.Leave("alice", out));
  ASSERT_FALSE(voice.Join("alice", "R1", out).has_value());
  const std::int64_t second = EpochOf("alice", out);
  ASSERT_NE(first, second);
  out.clear();
  EXPECT_EQ(Reason(voice.Signal("bob", Offer("alice", first), out)),
            "not in voice with that player");
  EXPECT_TRUE(out.empty());
  EXPECT_FALSE(voice.Signal("bob", Offer("alice", second), out).has_value());
  EXPECT_EQ(Staged(out), std::vector<std::string>{"alice:signal"});
}

TEST(VoiceTest, StunUrlsAreOneIceServerEachAndBlanksAreNone) {
  const auto servers = StunServersFromList(" stun:a.example:3478, ,stun:b.example ");
  ASSERT_EQ(servers.size(), 2u);
  EXPECT_EQ(servers[0].urls, std::vector<std::string>{"stun:a.example:3478"});
  EXPECT_EQ(servers[1].urls, std::vector<std::string>{"stun:b.example"});
  EXPECT_FALSE(servers[0].username.has_value());
  EXPECT_FALSE(servers[0].credential.has_value());
  EXPECT_TRUE(StunServersFromList("").empty());
  EXPECT_TRUE(StunServersFromList(" , ").empty());
}

}  // namespace
}  // namespace games_hub
