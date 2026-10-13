#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_DEFERRED_CLOSE_WEBSOCKET_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_DEFERRED_CLOSE_WEBSOCKET_H

// The server end of an in-memory test session, closing the way Beast's
// does: Close() returns at once and the close lands on another thread.
// The in-memory pair completes a parked Receive inline on the closing
// thread, so the hub closing a session parked in Receive (the registry's
// Close, a takeover's kick) would end that session's stream inside the
// handle call that pins it, and the stream's teardown would wait on that
// pin forever.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "opal/core/outcome.h"
#include "opal/eventstream/frame.h"
#include "opal/http/websocket.h"

namespace games_hub {

/// One thread that runs the deferred closes in order. Stop() runs what is
/// queued and joins; a close asked for after it runs inline, which is safe
/// once the fixture has closed every session itself.
class DeferredCloser {
 public:
  DeferredCloser() : thread_([this] { Run(); }) {}
  ~DeferredCloser() { Stop(); }
  DeferredCloser(const DeferredCloser&) = delete;
  DeferredCloser& operator=(const DeferredCloser&) = delete;

  void Post(std::shared_ptr<opal::http::WebSocket> socket) {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!stopped_) {
        queue_.push_back(std::move(socket));
        cv_.notify_one();
        return;
      }
    }
    socket->Close();
  }

  void Stop() {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      stopped_ = true;
    }
    cv_.notify_one();
    if (thread_.joinable()) thread_.join();
  }

 private:
  void Run() {
    std::unique_lock<std::mutex> lock(mu_);
    while (true) {
      cv_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
      if (queue_.empty()) return;
      auto socket = std::move(queue_.front());
      queue_.pop_front();
      lock.unlock();
      socket->Close();
      lock.lock();
    }
  }

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::shared_ptr<opal::http::WebSocket>> queue_;
  bool stopped_ = false;
  std::thread thread_;
};

class DeferredCloseWebSocket final : public opal::http::WebSocket {
 public:
  DeferredCloseWebSocket(std::shared_ptr<opal::http::WebSocket> inner,
                         std::shared_ptr<DeferredCloser> closer)
      : inner_(std::move(inner)), closer_(std::move(closer)) {}

  opal::Outcome<std::optional<opal::eventstream::Message>> Receive() override {
    return inner_->Receive();
  }
  opal::Outcome<std::optional<opal::eventstream::Message>> Receive(
      std::chrono::milliseconds timeout) override {
    return inner_->Receive(timeout);
  }
  opal::Outcome<opal::Unit> Send(const opal::eventstream::Message& message) override {
    return inner_->Send(message);
  }
  void Close() override { closer_->Post(inner_); }
  void ReceiveAsync(ReceiveCallback callback) override {
    inner_->ReceiveAsync(std::move(callback));
  }
  void ReceiveAsync(std::chrono::milliseconds timeout, ReceiveCallback callback) override {
    inner_->ReceiveAsync(timeout, std::move(callback));
  }
  void SendAsync(const opal::eventstream::Message& message, SendCallback callback) override {
    inner_->SendAsync(message, std::move(callback));
  }
  bool SupportsAsync() const override { return inner_->SupportsAsync(); }

 private:
  std::shared_ptr<opal::http::WebSocket> inner_;
  std::shared_ptr<DeferredCloser> closer_;
};

}  // namespace games_hub

#endif
