// Beyoncé Rule for smithy-cpp, golf_hub tier (see also
// domains/platform/libs/aura/smithy_contract_test.cc for the platform
// tier): the upstream behaviors the hub itself depends on, pinned as
// MoonBase-side contract tests so a smithy-cpp pin bump that changes any
// of them fails here with a named contract.
//
// What the hub relies on and where:
//   RequireOrigin           golf_hub_main.cc websocket_gate (ADR-0018)
//   InMemoryWebSocketPair   every hub session test harness
//   SessionRegistry         hub_handler.cc: ResumeOrAdd admission
//                           (ADR-0022), Detach + grace + on_expired
//                           (ADR-0020/0021), SendTo fan-out
//   Encode/DecodeJsonFrame  golf_hub_main.cc websocket_accept_json_frames
//                           (ADR-0018): the JSON text-frame codec every
//                           production browser session rides
//
// Deliberately non-Beast so it runs in restricted-egress sandboxes; the
// full transport path is covered by the hub e2e suites in CI.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "smithy/core/error.h"
#include "smithy/eventstream/envelope.h"
#include "smithy/eventstream/event_stream.h"
#include "smithy/eventstream/frame.h"
#include "smithy/eventstream/json_frame.h"
#include "smithy/http/message.h"
#include "smithy/http/websocket.h"
#include "smithy/http/websocket_pair.h"
#include "smithy/server/origin_gate.h"
#include "smithy/server/session_registry.h"

namespace {

using smithy::http::HttpRequest;

// --- RequireOrigin: the hub's cross-site-WebSocket-hijacking gate. ------

HttpRequest UpgradeWithOrigin(const std::string& origin) {
  HttpRequest request;
  if (!origin.empty()) {
    request.headers.Set("Origin", origin);
  }
  return request;
}

TEST(RequireOriginContract, AdmitsAllowlistedAndAbsentRefusesForeignAndNull) {
  auto gate = smithy::server::RequireOrigin({"https://muchq.com"});

  // Allowlisted origin and the no-Origin non-browser client are admitted.
  EXPECT_FALSE(gate(UpgradeWithOrigin("https://muchq.com")).has_value());
  EXPECT_FALSE(gate(UpgradeWithOrigin("")).has_value());

  // A foreign origin is refused with 403 — the attack this gate exists
  // for cannot omit the header.
  auto refusal = gate(UpgradeWithOrigin("https://evil.example"));
  ASSERT_TRUE(refusal.has_value());
  EXPECT_EQ(refusal->status, 403);

  // "null" (sandboxed iframe / file://) is refused unless allowlisted.
  EXPECT_TRUE(gate(UpgradeWithOrigin("null")).has_value());
}

TEST(RequireOriginContract, NormalizesCaseAndDefaultPorts) {
  auto gate = smithy::server::RequireOrigin({"https://muchq.com"});
  EXPECT_FALSE(gate(UpgradeWithOrigin("HTTPS://MUCHQ.COM")).has_value());
  EXPECT_FALSE(gate(UpgradeWithOrigin("https://muchq.com:443")).has_value());
  // Same host, wrong scheme is a different origin.
  EXPECT_TRUE(gate(UpgradeWithOrigin("http://muchq.com")).has_value());
}

// --- InMemoryWebSocketPair: the hub test harness's wire. -----------------

TEST(WebSocketPairContract, DeliversBothDirectionsAndCloseEndsCleanly) {
  auto [a, b] = smithy::http::InMemoryWebSocketPair::Create();

  smithy::eventstream::Message ping;
  ping.headers = {{":event-type", "ping"}};
  ping.payload = smithy::Blob::FromString("hello");
  ASSERT_TRUE(a->Send(ping).ok());

  auto received = b->Receive();
  ASSERT_TRUE(received.ok());
  ASSERT_TRUE(received.value().has_value());
  EXPECT_EQ(received.value()->payload.ToString(), "hello");

  smithy::eventstream::Message pong;
  pong.payload = smithy::Blob::FromString("yo");
  ASSERT_TRUE(b->Send(pong).ok());
  auto back = a->Receive();
  ASSERT_TRUE(back.ok());
  ASSERT_TRUE(back.value().has_value());

  // Close from one side: the peer's Receive observes the clean end
  // (ok + nullopt), which is what every hub serve loop breaks on.
  a->Close();
  auto ended = b->Receive();
  ASSERT_TRUE(ended.ok());
  EXPECT_FALSE(ended.value().has_value());
}

// --- Encode/DecodeJsonFrame: the ADR-0018 browser text wire. -------------
// golf_hub_main.cc sets websocket_accept_json_frames = true, so every
// production browser session's frames pass through this codec inside the
// transport. The hub's own wire tests drive the in-memory pair, which
// skips it — these pins are the only MoonBase-side coverage of the text
// bytes browsers actually parse.

TEST(JsonFrameContract, EventFramesRenderTheEnvelopeTextAndRoundTrip) {
  // Byte-pinned with a golf frame: this is the exact text a browser's
  // onmessage receives for the hub's first event. The codec's JSON output
  // is deterministic (compact, sorted keys), so string equality is wire
  // equality.
  const smithy::eventstream::Message ready = smithy::eventstream::MakeEventMessage(
      "sessionReady", "application/json",
      smithy::Blob::FromString(R"({"playerId":"player-1","resumed":false})"));
  const auto text = smithy::eventstream::EncodeJsonFrame(ready);
  ASSERT_TRUE(text.ok()) << text.error().message();
  EXPECT_EQ(*text, R"({"event":"sessionReady","payload":{"playerId":"player-1","resumed":false}})");

  // Decode gives back the Message the binary wire would have carried, so
  // everything above the socket is oblivious to the wire mode.
  const auto decoded = smithy::eventstream::DecodeJsonFrame(*text);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message();
  EXPECT_EQ(*decoded, ready);
}

TEST(JsonFrameContract, ExceptionFramesRenderTheExceptionMember) {
  // The terminal error arm — what a browser sees when a dial is refused
  // (golf_hub_wire_test pins the same refusal's binary framing).
  const smithy::eventstream::Message refusal = smithy::eventstream::MakeExceptionMessage(
      "Unauthenticated", "application/json",
      smithy::Blob::FromString(R"({"message":"ticket expired or already spent"})"));
  const auto text = smithy::eventstream::EncodeJsonFrame(refusal);
  ASSERT_TRUE(text.ok()) << text.error().message();
  EXPECT_EQ(*text, R"({"exception":"Unauthenticated",)"
                   R"("payload":{"message":"ticket expired or already spent"}})");

  const auto decoded = smithy::eventstream::DecodeJsonFrame(*text);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message();
  EXPECT_EQ(*decoded, refusal);
}

TEST(JsonFrameContract, HeadersBeyondTheEnvelopeAreRefusedNotDropped) {
  // The JSON envelope has no header channel. A message carrying any header
  // beyond the envelope trio must be REFUSED (Error::Validation, session
  // untouched) rather than encoded with the header silently dropped — the
  // refusal is what keeps a future hub from leaking a header-borne fact
  // into thin air on the production browser wire.
  smithy::eventstream::Message extra = smithy::eventstream::MakeEventMessage(
      "sessionReady", "application/json", smithy::Blob::FromString("{}"));
  extra.headers.push_back({"x-hub-extra", std::string("boom")});
  const auto text = smithy::eventstream::EncodeJsonFrame(extra);
  ASSERT_FALSE(text.ok());
  EXPECT_EQ(text.error().kind(), smithy::ErrorKind::kValidation);
}

// --- SessionRegistry: admission, grace, and fan-out (ADR-0020/21/22). ----
// Mirrors the hub's exact configuration: async_delivery=true (falls back
// to writer threads on sockets without async support, exactly as in the
// in-memory harness), a grace period, and on_expired.

struct Note {
  std::string text;
};

smithy::Outcome<smithy::eventstream::Message> EncodeNote(const Note& note) {
  smithy::eventstream::Message message;
  message.headers = {{":event-type", "note"}};
  message.payload = smithy::Blob::FromString(note.text);
  return message;
}

using ServerStream = smithy::eventstream::EventStream<Note, smithy::eventstream::NoEvents>;
using Registry = smithy::server::SessionRegistry<Note>;

struct Session {
  std::shared_ptr<smithy::http::WebSocket> client;
  std::unique_ptr<ServerStream> stream;
};

Session MakeSession() {
  auto [client_end, server_end] = smithy::http::InMemoryWebSocketPair::Create();
  Session session;
  session.client = client_end;
  session.stream = std::make_unique<ServerStream>(server_end, EncodeNote, nullptr);
  return session;
}

std::optional<std::string> ReceivePayload(smithy::http::WebSocket& socket) {
  auto received = socket.Receive(std::chrono::milliseconds(2000));
  if (!received.ok() || !received.value().has_value()) {
    return std::nullopt;
  }
  return received.value()->payload.ToString();
}

TEST(SessionRegistryContract, ResumeOrAddReportsAddedRefusedAndResumed) {
  Registry::Options options;
  options.grace_period = std::chrono::seconds(30);
  Registry registry(std::move(options));

  Session first = MakeSession();
  EXPECT_EQ(registry.ResumeOrAdd(
                "p1", [&first] { return first.stream->Share(); }, std::chrono::seconds(1)),
            Registry::Admission::kAdded);

  // Same seat while the first connection is live: refused (the hub's
  // SeatConflict path).
  Session intruder = MakeSession();
  EXPECT_EQ(registry.ResumeOrAdd(
                "p1", [&intruder] { return intruder.stream->Share(); }, std::chrono::seconds(1)),
            Registry::Admission::kRefused);

  // Detach parks the seat; a reconnect resumes it.
  EXPECT_TRUE(registry.Detach("p1"));
  Session reconnect = MakeSession();
  EXPECT_EQ(registry.ResumeOrAdd(
                "p1", [&reconnect] { return reconnect.stream->Share(); }, std::chrono::seconds(1)),
            Registry::Admission::kResumed);

  // Fan-out lands on the resumed connection.
  registry.SendTo("p1", Note{.text = "after-resume"});
  EXPECT_EQ(ReceivePayload(*reconnect.client).value_or(""), "after-resume");
}

TEST(SessionRegistryContract, GraceExpiryFiresOnExpiredExactlyOnce) {
  std::atomic<int> expired{0};
  Registry::Options options;
  options.grace_period = std::chrono::seconds(1);
  options.on_expired = [&expired](const std::string&) { expired++; };
  Registry registry(std::move(options));

  Session session = MakeSession();
  ASSERT_EQ(registry.ResumeOrAdd(
                "p1", [&session] { return session.stream->Share(); }, std::chrono::seconds(1)),
            Registry::Admission::kAdded);
  ASSERT_TRUE(registry.Detach("p1"));

  // The hub's OnExpired cleanup depends on exactly-once delivery after the
  // grace period.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (expired.load() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  EXPECT_EQ(expired.load(), 1);
}

TEST(SessionRegistryContract, SendToUnknownIdIsSafeAndRemoveCancelsSeat) {
  Registry registry{};

  // Fan-out to an id that never registered must be a harmless no-op.
  registry.SendTo("ghost", Note{.text = "into the void"});

  Session session = MakeSession();
  ASSERT_EQ(registry.ResumeOrAdd(
                "p1", [&session] { return session.stream->Share(); }, std::chrono::seconds(1)),
            Registry::Admission::kAdded);
  EXPECT_TRUE(registry.Remove("p1"));
  EXPECT_FALSE(registry.Remove("p1"));

  // After Remove the seat is free for a fresh add, not a resume.
  Session fresh = MakeSession();
  EXPECT_EQ(registry.ResumeOrAdd(
                "p1", [&fresh] { return fresh.stream->Share(); }, std::chrono::seconds(1)),
            Registry::Admission::kAdded);
}

}  // namespace
