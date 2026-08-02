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
/// channel. Dropped notifications during a disconnect gap are fine by
/// protocol: a notify is only a wake-up, and every wake re-reads current
/// state. Notifications absorbed into libpq during LISTEN/UNLISTEN are
/// not dropped — the poll loop drains PQnotifies after every sync pass
/// (#1276), not only when poll reports POLLIN.
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

  /// `on_active` is required: omitting it silently skips catch-up on
  /// (re)LISTEN, which is how room/chat fans lose commits across a gap.
  /// Pass an empty callback when the owner truly needs notify-only.
  Listener(std::string conninfo, Callback on_notify, ActiveCallback on_active);
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
  void SyncChannels(PGconn* conn);
  void DrainNotifies(PGconn* conn);

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
