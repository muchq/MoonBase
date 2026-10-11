#ifndef DOMAINS_GAMES_LIBS_ONE_D4_CPP_INDEX_QUEUE_H_
#define DOMAINS_GAMES_LIBS_ONE_D4_CPP_INDEX_QUEUE_H_

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "absl/status/status.h"

namespace one_d4 {

/// A player one_d4 should index for a month ("2026-10").
struct IndexAsk {
  std::string player_id;
  std::string month;

  bool operator==(const IndexAsk&) const = default;
};

/// Sends asks on a thread of its own, so a caller holding a lock never
/// waits on one_d4. Best effort: an ask already waiting is not queued
/// twice, past `capacity` new ones are dropped, and a send that fails is
/// logged and not retried. What is missed waits for the next ask for that
/// player, or an index asked for on 1d4.
class IndexQueue {
 public:
  using Send = std::function<absl::Status(const IndexAsk&)>;

  explicit IndexQueue(Send send, std::size_t capacity = 256);
  /// Stops the thread; asks still waiting are dropped.
  ~IndexQueue();

  IndexQueue(const IndexQueue&) = delete;
  IndexQueue& operator=(const IndexQueue&) = delete;

  /// Queues `ask`; never blocks on a send.
  void Submit(IndexAsk ask);

  /// Blocks until nothing waits and nothing is being sent.
  void Drain();

 private:
  void Run();

  Send send_;
  const std::size_t capacity_;
  std::mutex mu_;
  std::condition_variable wake_;
  std::condition_variable idle_;
  std::deque<IndexAsk> waiting_;
  bool sending_ = false;
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace one_d4

#endif  // DOMAINS_GAMES_LIBS_ONE_D4_CPP_INDEX_QUEUE_H_
