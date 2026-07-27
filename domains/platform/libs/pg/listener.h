#ifndef DOMAINS_PLATFORM_LIBS_PG_LISTENER_H
#define DOMAINS_PLATFORM_LIBS_PG_LISTENER_H

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "libpq-fe.h"

namespace pg {

/// The LISTEN side of #1194's fan-out: a dedicated connection whose poll
/// thread delivers notifications to one callback. Channels come and go
/// at runtime (the hub subscribes rooms as it learns them); a lost
/// connection reconnects with backoff and re-LISTENs every subscribed
/// channel. Dropped notifications during the gap are fine by protocol:
/// a notify is only a wake-up, and every wake re-reads current state.
///
/// The callbacks run on the listener's thread. They may call Listen and
/// Unlisten (they only flag work for the poll thread), but must not
/// block for long — nothing else is delivered while they run.
class Listener {
 public:
  using Callback = std::function<void(const std::string& channel, const std::string& payload)>;
  /// Fired after a channel's LISTEN succeeds on a connection — on first
  /// subscription and again after every reconnect's re-LISTEN. This is
  /// the "you may have missed notifications" signal: anything committed
  /// before this point never queued for us, so an owner that needs
  /// at-least-once delivery does a catch-up read when it fires.
  using ActiveCallback = std::function<void(const std::string& channel)>;

  Listener(std::string conninfo, Callback on_notify, ActiveCallback on_active = nullptr);
  ~Listener();
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  /// Idempotent; takes effect asynchronously (the poll thread issues the
  /// LISTEN). Channel names are quoted, so case and digits survive.
  void Listen(const std::string& channel);
  void Unlisten(const std::string& channel);

 private:
  void Loop();
  void Wake();
  /// (Re)connects and LISTENs the current channel set; returns false
  /// when stopping.
  bool ConnectLocked(std::unique_lock<std::mutex>& lock);
  void SyncChannels(PGconn* conn);

  const std::string conninfo_;
  const Callback on_notify_;
  const ActiveCallback on_active_;
  int wake_read_ = -1;
  int wake_write_ = -1;
  std::mutex mu_;
  std::set<std::string> wanted_;  // what the owner asked for
  std::set<std::string> active_;  // what the connection LISTENs today
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace pg

#endif
