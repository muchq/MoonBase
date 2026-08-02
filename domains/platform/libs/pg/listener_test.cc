#include "domains/platform/libs/pg/listener.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "domains/platform/libs/pg/pg.h"
#include "gtest/gtest.h"

namespace {

// Real-postgres suite, PG_TEST_DB_URL-gated like pg_test's healing case.
class ListenerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    url_ = std::getenv("PG_TEST_DB_URL");
    if (url_ == nullptr || *url_ == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
  }

  struct Received {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::pair<std::string, std::string>> events;

    void Add(const std::string& channel, const std::string& payload) {
      const std::lock_guard<std::mutex> lock(mu);
      events.emplace_back(channel, payload);
      cv.notify_all();
    }
    bool WaitForCount(std::size_t count, std::chrono::seconds timeout) {
      std::unique_lock<std::mutex> lock(mu);
      return cv.wait_for(lock, timeout, [&] { return events.size() >= count; });
    }
  };

  const char* url_ = nullptr;
};

TEST_F(ListenerTest, DeliversNotificationsAndChannelChurn) {
  Received received;
  pg::Listener listener(
      url_,
      [&](const std::string& channel, const std::string& payload) {
        received.Add(channel, payload);
      },
      /*on_active=*/nullptr);
  listener.Listen("room_ABC123");  // mixed case must survive quoting

  pg::Client client(url_);
  // The LISTEN is asynchronous; retry the first notify until it lands.
  bool delivered = false;
  for (int i = 0; i < 50 && !delivered; ++i) {
    ASSERT_TRUE(client.Exec("SELECT pg_notify('room_ABC123', 'hello')").ok());
    delivered = received.WaitForCount(1, std::chrono::seconds(1));
  }
  ASSERT_TRUE(delivered);
  EXPECT_EQ(received.events[0].first, "room_ABC123");
  EXPECT_EQ(received.events[0].second, "hello");

  // After Unlisten, notifies on that channel stop arriving.
  listener.Unlisten("room_ABC123");
  listener.Listen("other");
  bool other_delivered = false;
  for (int i = 0; i < 50 && !other_delivered; ++i) {
    ASSERT_TRUE(client.Exec("SELECT pg_notify('room_ABC123', 'ghost')").ok());
    ASSERT_TRUE(client.Exec("SELECT pg_notify('other', 'ping')").ok());
    std::unique_lock<std::mutex> lock(received.mu);
    other_delivered = received.cv.wait_for(lock, std::chrono::seconds(1), [&] {
      for (const auto& [channel, payload] : received.events) {
        if (channel == "other") return true;
      }
      return false;
    });
  }
  ASSERT_TRUE(other_delivered);
  // Unlisten applies asynchronously, in the same sync pass as the Listen
  // above — so ghosts sent before that pass may arrive, but none may
  // follow the first "other" delivery (notifications keep send order).
  const std::lock_guard<std::mutex> lock(received.mu);
  bool churned = false;
  for (const auto& [channel, payload] : received.events) {
    if (channel == "other") churned = true;
    if (churned) EXPECT_NE(payload, "ghost") << "received on unlistened channel";
  }
}

// The active callback removes the "retry until the LISTEN lands" dance:
// once it fires, the server-side LISTEN is in place, so a single notify
// is enough — this test sends each notify exactly once, no loops.
TEST_F(ListenerTest, ActiveFiresOnSubscribeAndAgainAfterReconnect) {
  Received notified;
  Received active;
  pg::Listener listener(
      url_,
      [&](const std::string& channel, const std::string& payload) {
        notified.Add(channel, payload);
      },
      [&](const std::string& channel) { active.Add(channel, ""); });
  listener.Listen("sync");

  ASSERT_TRUE(active.WaitForCount(1, std::chrono::seconds(10)));
  EXPECT_EQ(active.events[0].first, "sync");

  pg::Client client(url_);
  ASSERT_TRUE(client.Exec("SELECT pg_notify('sync', 'one')").ok());
  ASSERT_TRUE(notified.WaitForCount(1, std::chrono::seconds(10)));
  EXPECT_EQ(notified.events[0].first, "sync");
  EXPECT_EQ(notified.events[0].second, "one");

  // Kill the backend. The reconnect's re-LISTEN must announce the channel
  // active again — that second signal is what tells an owner it may have
  // missed notifications and should catch up.
  ASSERT_TRUE(client
                  .Exec("SELECT pg_terminate_backend(pid) FROM pg_stat_activity"
                        " WHERE query LIKE 'LISTEN%' AND pid <> pg_backend_pid()")
                  .ok());
  ASSERT_TRUE(active.WaitForCount(2, std::chrono::seconds(10)));
  EXPECT_EQ(active.events[1].first, "sync");

  ASSERT_TRUE(client.Exec("SELECT pg_notify('sync', 'two')").ok());
  ASSERT_TRUE(notified.WaitForCount(2, std::chrono::seconds(10)));
  EXPECT_EQ(notified.events[1].second, "two");
}

// #1276: a NOTIFY read into libpq during PQexec(LISTEN) must still be
// delivered when no later socket POLLIN arrives. Without an
// unconditional drain after SyncChannels, the victim stays queued
// forever and the owner hangs waiting for a wake that already happened.
TEST_F(ListenerTest, DrainsNotifyBufferedByListenExec) {
  Received received;
  Received active;
  std::mutex mu;
  std::condition_variable cv;
  bool holding = false;
  bool release = false;

  // Always release the poll thread, even if an ASSERT below fires —
  // otherwise ~Listener joins a callback that waits forever and the
  // small target dies at 60s with no failure text.
  struct ReleaseHold {
    std::mutex& mu;
    std::condition_variable& cv;
    bool& release;
    ~ReleaseHold() {
      {
        const std::lock_guard<std::mutex> lock(mu);
        release = true;
      }
      cv.notify_all();
    }
  } release_hold{mu, cv, release};

  pg::Listener listener(
      url_,
      [&](const std::string& channel, const std::string& payload) {
        if (payload == "hold") {
          std::unique_lock<std::mutex> lock(mu);
          holding = true;
          cv.notify_all();
          // Bounded: a stuck test must not pin the poll thread until the
          // Bazel ceiling.
          if (!cv.wait_for(lock, std::chrono::seconds(30), [&] { return release; })) {
            ADD_FAILURE() << "hold callback timed out waiting for release";
          }
        }
        received.Add(channel, payload);
      },
      [&](const std::string& channel) { active.Add(channel, ""); });

  listener.Listen("ch");
  ASSERT_TRUE(active.WaitForCount(1, std::chrono::seconds(10)));

  pg::Client client(url_);
  ASSERT_TRUE(client.Exec("SELECT pg_notify('ch', 'hold')").ok());
  {
    std::unique_lock<std::mutex> lock(mu);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&] { return holding; }));
  }

  // Poll thread is inside on_notify. Subscribe a new channel (wake →
  // SyncChannels → PQexec(LISTEN other)) and put the victim on the
  // wire. Once Exec returns, postgres has delivered to listening
  // backends — the bytes sit in the listener socket while the poll
  // thread is still held. Release then runs PQexec, which absorbs
  // them into libpq's queue with no POLLIN left for poll.
  listener.Listen("other");
  ASSERT_TRUE(client.Exec("SELECT pg_notify('ch', 'victim')").ok());
  {
    const std::lock_guard<std::mutex> lock(mu);
    release = true;
  }
  cv.notify_all();

  // SyncChannels ran iff "other" became active — pins the absorb path
  // to a real LISTEN pass rather than a later POLLIN delivery.
  ASSERT_TRUE(active.WaitForCount(2, std::chrono::seconds(10)))
      << "LISTEN other never completed after release";
  EXPECT_EQ(active.events[1].first, "other");

  ASSERT_TRUE(received.WaitForCount(2, std::chrono::seconds(10)))
      << "victim notify absorbed during LISTEN was never delivered";
  const std::lock_guard<std::mutex> lock(received.mu);
  ASSERT_EQ(received.events.size(), 2u);
  EXPECT_EQ(received.events[0].second, "hold");
  EXPECT_EQ(received.events[1].first, "ch");
  EXPECT_EQ(received.events[1].second, "victim");
}

TEST_F(ListenerTest, ReconnectsAndReListensAfterConnectionLoss) {
  Received received;
  pg::Listener listener(
      url_,
      [&](const std::string& channel, const std::string& payload) {
        received.Add(channel, payload);
      },
      /*on_active=*/nullptr);
  listener.Listen("heal");

  pg::Client client(url_);
  bool delivered = false;
  for (int i = 0; i < 50 && !delivered; ++i) {
    ASSERT_TRUE(client.Exec("SELECT pg_notify('heal', 'before')").ok());
    delivered = received.WaitForCount(1, std::chrono::seconds(1));
  }
  ASSERT_TRUE(delivered);

  // Kill the listener's backend out from under it; the poll thread must
  // reconnect and re-LISTEN on its own.
  ASSERT_TRUE(client
                  .Exec("SELECT pg_terminate_backend(pid) FROM pg_stat_activity"
                        " WHERE query LIKE 'LISTEN%' AND pid <> pg_backend_pid()")
                  .ok());

  const std::size_t before = [&] {
    const std::lock_guard<std::mutex> lock(received.mu);
    return received.events.size();
  }();
  bool healed = false;
  for (int i = 0; i < 100 && !healed; ++i) {
    ASSERT_TRUE(client.Exec("SELECT pg_notify('heal', 'after')").ok());
    healed = received.WaitForCount(before + 1, std::chrono::seconds(1));
  }
  ASSERT_TRUE(healed);
}

}  // namespace
