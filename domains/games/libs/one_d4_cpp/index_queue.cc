#include "domains/games/libs/one_d4_cpp/index_queue.h"

#include <algorithm>
#include <utility>

#include "absl/log/log.h"

namespace one_d4 {

IndexQueue::IndexQueue(Send send, std::size_t capacity)
    : send_(std::move(send)), capacity_(capacity), thread_([this] { Run(); }) {}

IndexQueue::~IndexQueue() {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    stopping_ = true;
  }
  wake_.notify_all();
  thread_.join();
}

void IndexQueue::Submit(IndexAsk ask) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    if (std::find(waiting_.begin(), waiting_.end(), ask) != waiting_.end()) return;
    if (waiting_.size() >= capacity_) {
      LOG(WARNING) << "1d4 index ask for " << ask.player_id << " dropped: " << capacity_
                   << " already waiting";
      return;
    }
    waiting_.push_back(std::move(ask));
  }
  wake_.notify_one();
}

void IndexQueue::Drain() {
  std::unique_lock<std::mutex> lock(mu_);
  idle_.wait(lock, [this] { return waiting_.empty() && !sending_; });
}

void IndexQueue::Run() {
  std::unique_lock<std::mutex> lock(mu_);
  while (true) {
    wake_.wait(lock, [this] { return stopping_ || !waiting_.empty(); });
    if (stopping_) return;
    IndexAsk ask = std::move(waiting_.front());
    waiting_.pop_front();
    sending_ = true;
    lock.unlock();
    if (const absl::Status sent = send_(ask); !sent.ok()) {
      LOG(WARNING) << "1d4 index ask for " << ask.player_id << " " << ask.month
                   << " failed: " << sent;
    }
    lock.lock();
    sending_ = false;
    idle_.notify_all();
  }
}

}  // namespace one_d4
