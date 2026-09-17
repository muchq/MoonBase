// deja on the walls (#1554, #1150): the hub is deja's second consumer and
// the tape is world state it owns, so this suite drives the poller the way
// production does — through the raw wire, with a scripted deja underneath —
// and pins what reaches the glass.
//
// What it covers: the occupancy gate (no glasshouse occupant, no HTTP at
// all), the splat bytes the browser reads, that two clients in one room
// land the same event on the same spot, dedupe by seq, resuming from the
// newest seq instead of replaying a backlog, and a deja that is down,
// slow or lying doing none of that damage.
//
// The poll cycle is called directly rather than through the poll thread:
// the thread is PollTapeOnce on a one-to-two-second tick, and nothing here
// is improved by waiting for one.

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "domains/ai/libs/deja_cpp/client.h"
#include "domains/games/apis/games_hub/splat.h"
#include "domains/games/apis/games_hub/wire_test_fixture.h"
#include "opal/core/error.h"
#include "opal/http/message.h"
#include "opal/http/transport.h"

namespace games_hub {
namespace {

using json = nlohmann::json;

constexpr char kPlayPath[] = "/games/v2/play";
constexpr char kGlasshouse[] = R"({"action":{"setGeometry":{"geometry":{"glasshouse":{}}}}})";
constexpr char kJoinPayload[] =
    R"({"action":{"join":{"position":[10,0,-5],"color":[0.8,0.2,0.6],"shape":0}}})";

// One deja event, in deja's own spelling (recent_wire_test pins the real
// bytes; this builds more of them).
std::string Event(int seq, const std::string& actual, const std::string& verdict) {
  return R"({"seq":)" + std::to_string(seq) +
         R"(,"ts":1789500001.5,"lane":0,"step":1000,"context":["muchq.com GET / 200 browser"],)"
         R"("actual":")" +
         actual +
         R"(","predictions":{"bigram":[{"token":"muchq.com GET / 200 browser","p":0.75}],)"
         R"("net":[{"token":"muchq.com GET /golf 200 browser","p":0.5}]},)"
         R"("surprise":{"bigram":9.5,"net":8.25},"threshold":6.5,"verdict":")" +
         verdict + R"(","ewma_loss":{"bigram":1.75,"net":1.5},"vocab_size":41})";
}

std::string Tape(const std::vector<std::string>& events) {
  std::string body = R"({"events":[)";
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (i > 0) body += ",";
    body += events[i];
  }
  return body + "]}";
}

// A deja under the test's thumb: it answers whatever was queued, records
// every `after` it was asked, and can be told to be dead or to take its
// time. Every method is locked because the poll thread may be the caller.
class ScriptedDeja final : public opal::http::HttpClient {
 public:
  opal::Outcome<opal::http::HttpResponse> Send(const opal::http::HttpRequest& request) override {
    std::string body;
    {
      std::unique_lock<std::mutex> lock(mu_);
      asked_.push_back(request.target);
      if (gated_) {
        ++parked_;
        // The ceiling is deliberate: a hub that took mu_ across this call
        // would deadlock against the test thread, and a suite that hangs
        // says far less than one that fails.
        gate_.wait_for(lock, kGateCeiling, [this] { return !gated_; });
        --parked_;
      }
      if (dead_) return opal::Error::Unknown("connection refused");
      body = next_;
    }
    opal::http::HttpResponse response;
    response.status = 200;
    response.body = body;
    return response;
  }

  void Answer(std::string body) {
    const std::lock_guard<std::mutex> lock(mu_);
    next_ = std::move(body);
    dead_ = false;
  }
  void Die() {
    const std::lock_guard<std::mutex> lock(mu_);
    dead_ = true;
  }
  /// Parks every later Send until Release.
  void Gate() {
    const std::lock_guard<std::mutex> lock(mu_);
    gated_ = true;
  }
  void Release() {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      gated_ = false;
    }
    gate_.notify_all();
  }
  /// Whether a poll is parked inside deja right now.
  bool parked() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return parked_ > 0;
  }
  std::vector<std::string> asked() const {
    const std::lock_guard<std::mutex> lock(mu_);
    return asked_;
  }

  static constexpr std::chrono::seconds kGateCeiling{3};

 private:
  mutable std::mutex mu_;
  std::condition_variable gate_;
  std::vector<std::string> asked_;
  std::string next_ = R"({"events":[]})";
  bool gated_ = false;
  int parked_ = 0;
  bool dead_ = false;
};

// Spins until `ready` or the deadline, so a test that is about to assert
// on an in-flight poll is not asserting on one that never started.
template <typename Ready>
bool WaitFor(const Ready& ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return ready();
}

class TapeTest : public HubWireFixture {
 protected:
  void SetUp() override {
    HubWireFixture::SetUp();
    opal::ClientConfig config = deja::DefaultClientConfig("http://deja:8093");
    config.http_client = deja_;
    auto client = deja::Client::Create(std::move(config));
    ASSERT_TRUE(client.ok()) << client.error().message();
    golf_->AttachTape(std::make_shared<deja::Client>(std::move(*client)));
  }

  std::shared_ptr<opal::http::WebSocket> DialReady(json& session) {
    return HubWireFixture::DialReady(kPlayPath, session);
  }

  // A session standing in a glasshouse world of its own room, with every
  // frame the setup produced already drained.
  std::shared_ptr<opal::http::WebSocket> StandOnGlass(json& session) {
    auto socket = DialReady(session);
    EXPECT_TRUE(socket->Send(CommandFrame("createRoom", "{}")).ok());
    (void)EventPayload(NextFrame(*socket), "roomState");
    EXPECT_TRUE(socket->Send(CommandFrame("lobby", kGlasshouse)).ok());
    EXPECT_TRUE(socket->Send(CommandFrame("lobby", kJoinPayload)).ok());
    (void)EventPayload(NextFrame(*socket), "lobby");
    return socket;
  }

  // Drains one poll's worth: PollTapeOnce, then the next lobby frame.
  std::string PollAndRead(opal::http::WebSocket& socket) {
    EXPECT_TRUE(golf_->PollTapeOnce());
    return EventPayload(NextFrame(socket), "lobby");
  }

  std::shared_ptr<ScriptedDeja> deja_ = std::make_shared<ScriptedDeja>();
};

// The gate, and the whole reason the tape costs nothing when nobody is
// looking: a hub with no glasshouse occupant sends deja not one byte.
TEST_F(TapeTest, NoGlasshouseOccupantMeansNoHttpAtAll) {
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  // Nobody connected at all.
  EXPECT_FALSE(golf_->PollTapeOnce());

  // Connected, in a room, but standing on the plane the room starts with.
  json session;
  auto socket = DialReady(session);
  ASSERT_TRUE(socket->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*socket), "lobby");
  EXPECT_FALSE(golf_->PollTapeOnce());

  // A sphere is not glass either.
  ASSERT_TRUE(socket
                  ->Send(CommandFrame("lobby", R"({"action":{"setGeometry":{"geometry":)"
                                               R"({"sphere":{"radius":53}}}}})"))
                  .ok());
  (void)EventPayload(NextFrame(*socket), "lobby");
  EXPECT_FALSE(golf_->PollTapeOnce());

  EXPECT_TRUE(deja_->asked().empty()) << "deja was asked with nobody on any glass";

  // The first standing occupant of a glasshouse opens it...
  ASSERT_TRUE(socket->Send(CommandFrame("lobby", kGlasshouse)).ok());
  (void)EventPayload(NextFrame(*socket), "lobby");
  EXPECT_TRUE(golf_->PollTapeOnce());
  EXPECT_EQ(deja_->asked().size(), 1u);

  // ...and the last leaver closes it again.
  ASSERT_TRUE(socket->Send(CommandFrame("lobby", R"({"action":{"leave":{}}})")).ok());
  EXPECT_FALSE(golf_->PollTapeOnce());
  EXPECT_EQ(deja_->asked().size(), 1u) << "the tape kept polling after the glass emptied";
}

// Consumer: the bytes a browser standing in a glasshouse reads. The splat
// point is the hub's — derived from seq, never sent by deja — and the
// chips, the two top guesses, the actual token and the verdict ride with
// it. Nothing about the wall's height is on the wire.
TEST_F(TapeTest, ATapeSplatPinsItsBytes) {
  json session;
  auto socket = StandOnGlass(session);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  // The first poll takes the newest seq and shows nothing (see below).
  ASSERT_TRUE(golf_->PollTapeOnce());
  deja_->Answer(Tape({Event(2, "muchq.com GET /wp-login.php 404 bot probe", "anomaly")}));

  EXPECT_EQ(PollAndRead(*socket),
            R"({"update":{"tape":{"actual":"muchq.com GET /wp-login.php 404 bot probe",)"
            R"("bigram":{"p":0.75,"token":"muchq.com GET / 200 browser"},)"
            R"("context":["muchq.com GET / 200 browser"],)"
            R"("net":{"p":0.5,"token":"muchq.com GET /golf 200 browser"},"seq":2,)"
            R"("u":0.11168384552001953,"v":0.7295174598693848,"verdict":"anomaly","wall":2}}})");
  EXPECT_EQ(deja_->asked().back(), "/deja/v1/recent?after=1");
}

// The determinism that makes the wall a shared object rather than two
// private ones: both clients in the room get the same seq on the same
// wall at the same spot, and that spot is what SplatFor says it is.
TEST_F(TapeTest, EveryClientInTheRoomLandsTheEventOnTheSameSpot) {
  json host_session;
  auto host = StandOnGlass(host_session);

  json guest_session;
  auto guest = DialReady(guest_session);
  ASSERT_TRUE(guest->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*guest), "roomState");
  (void)EventPayload(NextFrame(*guest), "roomChatHistory");
  (void)EventPayload(NextFrame(*host), "roomState");
  ASSERT_TRUE(guest->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*guest), "lobby");
  (void)EventPayload(NextFrame(*host), "lobby");

  deja_->Answer(Tape({Event(100, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());  // priming
  deja_->Answer(Tape({Event(101, "muchq.com GET /thoughts 200 browser", "novel"),
                      Event(102, "muchq.com GET /golf 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());

  for (const int seq : {101, 102}) {
    const json to_host = json::parse(EventPayload(NextFrame(*host), "lobby"))["update"]["tape"];
    const json to_guest = json::parse(EventPayload(NextFrame(*guest), "lobby"))["update"]["tape"];
    EXPECT_EQ(to_host, to_guest) << "seq " << seq << " landed in two different places";
    EXPECT_EQ(to_host["seq"].get<int>(), seq);
    const Splat expected = SplatFor(seq);
    EXPECT_EQ(to_host["wall"].get<int>(), expected.wall);
    EXPECT_DOUBLE_EQ(to_host["u"].get<double>(), expected.u);
    EXPECT_DOUBLE_EQ(to_host["v"].get<double>(), expected.v);
  }
}

// A late joiner walks into a wall that already has tape on it, and gets
// exactly what the people already in the room saw.
TEST_F(TapeTest, ALateJoinerIsHandedTheWallAsItStands) {
  json host_session;
  auto host = StandOnGlass(host_session);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());  // priming
  deja_->Answer(Tape({Event(2, "muchq.com GET /golf 200 browser", "expected"),
                      Event(3, "muchq.com GET /thoughts 200 browser", "novel")}));
  ASSERT_TRUE(golf_->PollTapeOnce());
  const json first = json::parse(EventPayload(NextFrame(*host), "lobby"))["update"]["tape"];
  const json second = json::parse(EventPayload(NextFrame(*host), "lobby"))["update"]["tape"];

  json guest_session;
  auto guest = DialReady(guest_session);
  ASSERT_TRUE(guest->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*guest), "roomState");
  (void)EventPayload(NextFrame(*guest), "roomChatHistory");
  (void)EventPayload(NextFrame(*host), "roomState");
  ASSERT_TRUE(guest->Send(CommandFrame("lobby", kJoinPayload)).ok());

  const json world = json::parse(EventPayload(NextFrame(*guest), "lobby"))["update"]["worldState"];
  EXPECT_EQ(world["geometry"], json::parse(R"({"glasshouse":{}})"));
  ASSERT_TRUE(world.contains("tape"));
  EXPECT_EQ(world["tape"], json::array({first, second}));
}

// deja's ring re-serves what it already handed over; seq is the identity,
// and a splat is drawn once.
TEST_F(TapeTest, AnEventAlreadyOnTheWallIsNotSplattedTwice) {
  json session;
  auto socket = StandOnGlass(session);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());  // priming
  deja_->Answer(Tape({Event(2, "muchq.com GET /golf 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*socket), "lobby"))["update"]["tape"]["seq"], 2);

  // deja answers with the old event again, plus one new one.
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected"),
                      Event(2, "muchq.com GET /golf 200 browser", "expected"),
                      Event(3, "muchq.com GET /thoughts 200 browser", "novel")}));
  ASSERT_TRUE(golf_->PollTapeOnce());

  // The next frame is 3, not a redraw of 1 or 2.
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*socket), "lobby"))["update"]["tape"]["seq"], 3);
}

// An empty room is not a debt. When the glass fills again the wall starts
// from what is happening now, not from the minutes nobody watched.
TEST_F(TapeTest, ResumingAfterAnEmptyRoomStartsFromTheNewestSeq) {
  json session;
  auto socket = StandOnGlass(session);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());  // the very first poll primes too

  ASSERT_TRUE(socket->Send(CommandFrame("lobby", R"({"action":{"leave":{}}})")).ok());
  EXPECT_FALSE(golf_->PollTapeOnce());

  // Ten minutes of traffic nobody was there for.
  std::vector<std::string> backlog;
  for (int seq = 2; seq <= 60; ++seq) {
    backlog.push_back(Event(seq, "muchq.com GET / 200 browser", "expected"));
  }
  deja_->Answer(Tape(backlog));

  ASSERT_TRUE(socket->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*socket), "lobby");
  ASSERT_TRUE(golf_->PollTapeOnce());

  // Nothing from the backlog reaches the glass...
  deja_->Answer(Tape({Event(61, "muchq.com GET /golf 200 browser", "novel")}));
  ASSERT_TRUE(golf_->PollTapeOnce());
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*socket), "lobby"))["update"]["tape"]["seq"], 61)
      << "the backlog was replayed onto the wall";
  // ...and the resume asked from the newest seq the backlog carried.
  EXPECT_EQ(deja_->asked().back(), "/deja/v1/recent?after=60");
}

// Best effort means best effort: deja dead, and deja answering nonsense,
// each cost one counted poll and nothing else. The room never hears a word
// of it and still works afterwards, which is the assertion that matters.
TEST_F(TapeTest, ADejaThatIsDownOrLyingNeverReachesTheRoom) {
  json session;
  auto socket = StandOnGlass(session);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());  // priming

  deja_->Die();
  EXPECT_TRUE(golf_->PollTapeOnce());
  deja_->Answer(R"({"events":"not a list"})");
  EXPECT_TRUE(golf_->PollTapeOnce());
  deja_->Answer("<html>502 Bad Gateway</html>");
  EXPECT_TRUE(golf_->PollTapeOnce());

  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_polls", {{"result", "failed"}}), 3);
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_splats", {}), 0);

  deja_->Answer(Tape({Event(2, "muchq.com GET /golf 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*socket), "lobby"))["update"]["tape"]["seq"], 2);
}

// The discipline the listener's catch-up already follows, pinned rather
// than argued: mu_ is not held across the round trip. With a poll parked
// inside deja, a move still crosses the room — and it must arrive while
// the poll is STILL parked, which is an ordering, not a stopwatch.
TEST_F(TapeTest, ASlowDejaDoesNotHoldTheHubLock) {
  json host_session;
  auto host = StandOnGlass(host_session);
  json guest_session;
  auto guest = DialReady(guest_session);
  ASSERT_TRUE(guest->Send(CommandFrame("joinRoom", R"({"roomId":"room-1"})")).ok());
  (void)EventPayload(NextFrame(*guest), "roomState");
  (void)EventPayload(NextFrame(*guest), "roomChatHistory");
  (void)EventPayload(NextFrame(*host), "roomState");
  ASSERT_TRUE(guest->Send(CommandFrame("lobby", kJoinPayload)).ok());
  (void)EventPayload(NextFrame(*guest), "lobby");
  (void)EventPayload(NextFrame(*host), "lobby");

  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());  // priming, ungated
  deja_->Answer(Tape({Event(2, "muchq.com GET /golf 200 browser", "expected")}));
  deja_->Gate();
  std::thread poll([this] { golf_->PollTapeOnce(); });
  ASSERT_TRUE(WaitFor([this] { return deja_->parked(); })) << "the poll never reached deja";

  ASSERT_TRUE(
      guest->Send(CommandFrame("lobby", R"({"action":{"move":{"position":[11,0,-5]}}})")).ok());
  const std::string moved = EventPayload(NextFrame(*host), "lobby");
  EXPECT_TRUE(deja_->parked())
      << "the room only moved once deja answered: mu_ was held across the round trip";
  EXPECT_EQ(moved,
            R"({"update":{"playerMoved":{"playerId":"player-2","position":[11.0,0.0,-5.0]}}})");

  deja_->Release();
  poll.join();
  // And the splat that was waiting on deja lands afterwards, on both.
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*host), "lobby"))["update"]["tape"]["seq"], 2);
  EXPECT_EQ(json::parse(EventPayload(NextFrame(*guest), "lobby"))["update"]["tape"]["seq"], 2);
}

// The counters the dashboard reads (#1327): polls by result, splats fanned
// out, and the gauge saying whether this instance is polling at all.
TEST_F(TapeTest, TheTapeCountsWhatItAsksForAndWhatItShows) {
  json session;
  auto socket = StandOnGlass(session);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));
  ASSERT_TRUE(golf_->PollTapeOnce());
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_polls", {{"result", "ok"}}), 1);
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_splats", {}), 0) << "a priming poll shows nothing";

  deja_->Answer(Tape({Event(2, "muchq.com GET /golf 200 browser", "expected"),
                      Event(3, "muchq.com GET /thoughts 200 browser", "novel")}));
  ASSERT_TRUE(golf_->PollTapeOnce());
  (void)EventPayload(NextFrame(*socket), "lobby");
  (void)EventPayload(NextFrame(*socket), "lobby");
  // Two events on one wall, not two per viewer and not one per poll.
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_splats", {}), 2);
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_polls", {{"result", "ok"}}), 2);
  EXPECT_EQ(metrics_->CounterTotal("lobby_events", {{"event", "tape"}}), 2);
  // The gauge is a delta (up-down) series, so its sum is its level.
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_poller_active", {}), 1);

  ASSERT_TRUE(socket->Send(CommandFrame("lobby", R"({"action":{"leave":{}}})")).ok());
  EXPECT_FALSE(golf_->PollTapeOnce());
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_poller_active", {}), 0);
  EXPECT_EQ(metrics_->CounterTotal("lobby_tape_polls", {{"result", "ok"}}), 2)
      << "an unoccupied cycle is not a poll";
}

// The poll thread is PollTapeOnce on a tick, and the hub joins it: a hub
// torn down mid-poll must not leave a thread holding a dead `this`.
TEST_F(TapeTest, TheThreadPollsAndTheHubJoinsIt) {
  json session;
  auto socket = StandOnGlass(session);
  ASSERT_NE(socket, nullptr);
  deja_->Answer(Tape({Event(1, "muchq.com GET / 200 browser", "expected")}));

  golf_->StartTapePolling();
  golf_->StartTapePolling();  // idempotent; a second thread would double-poll

  EXPECT_TRUE(WaitFor([this] { return !deja_->asked().empty(); }))
      << "the poll thread never asked deja anything";
}

}  // namespace
}  // namespace games_hub
