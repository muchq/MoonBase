#include "domains/games/libs/one_d4_cpp/index_queue.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <vector>

#include "absl/status/status.h"

namespace one_d4 {
namespace {

using ::testing::ElementsAre;

// Sends, in the order asked, on the queue's own thread.
TEST(IndexQueue, SendsEachAskInOrder) {
  std::mutex mu;
  std::vector<IndexAsk> sent;
  IndexQueue queue([&](const IndexAsk& ask) {
    const std::lock_guard<std::mutex> lock(mu);
    sent.push_back(ask);
    return absl::OkStatus();
  });
  queue.Submit({"alice", "2026-10"});
  queue.Submit({"bob", "2026-10"});
  queue.Drain();
  EXPECT_THAT(sent, ElementsAre(IndexAsk{"alice", "2026-10"}, IndexAsk{"bob", "2026-10"}));
}

// A send held up holds up nobody: Submit returns while one is in flight;
// an ask already waiting is not queued twice; past capacity, new ones drop.
TEST(IndexQueue, NeverWaitsOnASendQueuesNoDuplicateAndDropsPastCapacity) {
  std::mutex mu;
  std::condition_variable cv;
  bool release = false;
  bool in_flight = false;
  std::vector<IndexAsk> sent;
  IndexQueue queue(
      [&](const IndexAsk& ask) {
        std::unique_lock<std::mutex> lock(mu);
        in_flight = true;
        cv.notify_all();
        cv.wait(lock, [&] { return release; });
        sent.push_back(ask);
        return absl::OkStatus();
      },
      /*capacity=*/2);
  queue.Submit({"alice", "2026-10"});
  {
    std::unique_lock<std::mutex> lock(mu);
    cv.wait(lock, [&] { return in_flight; });
  }
  queue.Submit({"bob", "2026-10"});
  queue.Submit({"bob", "2026-10"});  // already waiting
  queue.Submit({"carol", "2026-10"});
  queue.Submit({"dave", "2026-10"});  // past capacity
  {
    const std::lock_guard<std::mutex> lock(mu);
    release = true;
  }
  cv.notify_all();
  queue.Drain();
  EXPECT_THAT(sent, ElementsAre(IndexAsk{"alice", "2026-10"}, IndexAsk{"bob", "2026-10"},
                                IndexAsk{"carol", "2026-10"}));
}

// A failed send is not retried, and the next ask still goes out.
TEST(IndexQueue, AFailedSendIsDroppedAndTheNextStillGoes) {
  std::mutex mu;
  std::vector<std::string> tried;
  IndexQueue queue([&](const IndexAsk& ask) {
    const std::lock_guard<std::mutex> lock(mu);
    tried.push_back(ask.player_id);
    return ask.player_id == "alice" ? absl::UnavailableError("1d4 is down") : absl::OkStatus();
  });
  queue.Submit({"alice", "2026-10"});
  queue.Submit({"bob", "2026-10"});
  queue.Drain();
  EXPECT_THAT(tried, ElementsAre("alice", "bob"));
}

}  // namespace
}  // namespace one_d4
