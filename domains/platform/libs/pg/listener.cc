#include "domains/platform/libs/pg/listener.h"

#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace pg {
namespace {

// LISTEN takes an identifier, not a parameter; the channel must be
// quoted or postgres case-folds it away from what pg_notify() sent.
std::string QuotedListen(PGconn* conn, const std::string& verb, const std::string& channel) {
  char* quoted = PQescapeIdentifier(conn, channel.c_str(), channel.size());
  if (quoted == nullptr) return "";
  std::string sql = absl::StrCat(verb, " ", quoted);
  PQfreemem(quoted);
  return sql;
}

bool RunOrWarn(PGconn* conn, const std::string& sql) {
  if (sql.empty()) return false;
  PGresult* result = PQexec(conn, sql.c_str());
  const bool ok = result != nullptr && PQresultStatus(result) == PGRES_COMMAND_OK;
  if (!ok) LOG(WARNING) << "listener statement failed: " << sql;
  if (result != nullptr) PQclear(result);
  return ok;
}

}  // namespace

Listener::Listener(std::string conninfo, Callback on_notify, ActiveCallback on_active)
    : conninfo_(std::move(conninfo)),
      on_notify_(std::move(on_notify)),
      on_active_(std::move(on_active)) {
  int fds[2] = {-1, -1};
  if (pipe(fds) == 0) {
    wake_read_ = fds[0];
    wake_write_ = fds[1];
  }
  thread_ = std::thread([this] { Loop(); });
}

Listener::~Listener() {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    stopping_ = true;
  }
  Wake();
  thread_.join();
  if (wake_read_ >= 0) close(wake_read_);
  if (wake_write_ >= 0) close(wake_write_);
}

void Listener::Listen(const std::string& channel) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    if (!wanted_.insert(channel).second) return;
  }
  Wake();
}

void Listener::Unlisten(const std::string& channel) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    if (wanted_.erase(channel) == 0) return;
  }
  Wake();
}

void Listener::Wake() {
  if (wake_write_ >= 0) {
    const char byte = 'w';
    [[maybe_unused]] const auto written = write(wake_write_, &byte, 1);
  }
}

void Listener::SyncChannels(PGconn* conn) {
  std::set<std::string> wanted;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    wanted = wanted_;
  }
  // active_ tracks what the server actually LISTENs, so a failed
  // statement stays out of it and is retried on the next pass (the poll
  // bounds each pass at a second) instead of being assumed through.
  std::vector<std::string> newly_active;
  for (const std::string& channel : wanted) {
    if (active_.contains(channel)) continue;
    if (RunOrWarn(conn, QuotedListen(conn, "LISTEN", channel))) {
      active_.insert(channel);
      newly_active.push_back(channel);
    }
  }
  for (auto it = active_.begin(); it != active_.end();) {
    if (wanted.contains(*it)) {
      ++it;
    } else if (RunOrWarn(conn, QuotedListen(conn, "UNLISTEN", *it))) {
      it = active_.erase(it);
    } else {
      ++it;  // failed UNLISTEN: still subscribed server-side, retry
    }
  }
  // Off-mutex by construction — active_ belongs to this thread alone —
  // so the owner may call Listen/Unlisten from inside the callback.
  if (on_active_) {
    for (const std::string& channel : newly_active) on_active_(channel);
  }
}

void Listener::Loop() {
  std::unique_ptr<PGconn, decltype(&PQfinish)> conn(nullptr, &PQfinish);
  auto backoff = std::chrono::milliseconds(100);
  while (true) {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (stopping_) return;
    }
    if (conn == nullptr || PQstatus(conn.get()) != CONNECTION_OK) {
      conn.reset(PQconnectdb(conninfo_.c_str()));
      if (conn == nullptr || PQstatus(conn.get()) != CONNECTION_OK) {
        LOG(WARNING) << "listener connect failed; retrying";
        conn.reset();
        std::this_thread::sleep_for(backoff);
        backoff = std::min(backoff * 2, std::chrono::milliseconds(2000));
        continue;
      }
      backoff = std::chrono::milliseconds(100);
      active_.clear();  // fresh connection LISTENs from scratch
    }
    SyncChannels(conn.get());

    struct pollfd fds[2];
    fds[0] = {PQsocket(conn.get()), POLLIN, 0};
    fds[1] = {wake_read_, POLLIN, 0};
    // Bounded wait so a torn-down socket can't park us forever.
    if (poll(fds, wake_read_ >= 0 ? 2 : 1, /*timeout_ms=*/1000) < 0) continue;

    if (wake_read_ >= 0 && (fds[1].revents & POLLIN) != 0) {
      char drain[64];
      while (read(wake_read_, drain, sizeof(drain)) == sizeof(drain)) {
      }
    }
    if ((fds[0].revents & POLLIN) != 0) {
      if (PQconsumeInput(conn.get()) == 0) {
        LOG(WARNING) << "listener connection lost; reconnecting";
        conn.reset();
        continue;
      }
      while (PGnotify* notify = PQnotifies(conn.get())) {
        on_notify_(notify->relname, notify->extra == nullptr ? "" : notify->extra);
        PQfreemem(notify);
      }
    }
  }
}

}  // namespace pg
