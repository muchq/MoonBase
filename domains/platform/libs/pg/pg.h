#ifndef DOMAINS_PLATFORM_LIBS_PG_PG_H
#define DOMAINS_PLATFORM_LIBS_PG_PG_H

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

// libpq's opaque handles, forward-declared so consumers don't inherit
// libpq-fe.h.
struct pg_conn;
struct pg_result;

namespace pg {

/// One statement's rows, text format. Owns the libpq result.
class Result {
 public:
  explicit Result(pg_result* result);
  ~Result();
  Result(Result&& other) noexcept;
  Result& operator=(Result&& other) noexcept;
  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;

  int rows() const;
  int columns() const;
  /// The value at (row, column); nullopt for SQL NULL or out of range.
  std::optional<std::string> Get(int row, int column) const;

 private:
  pg_result* result_;
};

/// The owned libpq wrapper (#1194: "libpq behind a small owned wrapper —
/// same instinct as the rest of the platform libs"). One blocking
/// connection, serialized by a mutex, text-format parameters: what
/// credential and per-move traffic needs. Connections are established
/// lazily and re-established once per Exec after a connection-level
/// failure, so a postgres restart heals on the next call.
///
/// Deliberately not here yet: pooling, async I/O, LISTEN/NOTIFY — the
/// fan-out step of #1194 grows this surface when an event loop needs it.
class Client {
 public:
  /// conninfo is any libpq connection string, including
  /// postgresql://user:pass@host:port/db URIs.
  explicit Client(std::string conninfo);
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  /// Runs one statement. params bind as $1..$N, text format. After a
  /// connection-level failure Exec reconnects and retries once, so a
  /// statement may execute twice on the server. Callers must judge that
  /// risk per statement: a non-idempotent write whose first execution
  /// landed reports its retry's result (e.g. a DELETE that already
  /// deleted reports zero rows).
  absl::StatusOr<Result> Exec(const std::string& sql, const std::vector<std::string>& params = {});

  /// Exec without the retry: the statement reaches the server at most
  /// once, and a connection-level failure is reported rather than
  /// retried. Connecting is still automatic — only re-running a
  /// statement whose outcome is unknown is what this gives up.
  ///
  /// Use it for a write whose second execution would be visible. A
  /// conditional write (games) is safe to retry because the condition
  /// misses the second time; an unconditional append is not, and a
  /// duplicate row carrying a fresh id is one no consumer can dedupe.
  absl::StatusOr<Result> ExecOnce(const std::string& sql,
                                  const std::vector<std::string>& params = {});

 private:
  absl::Status EnsureConnectedLocked();
  absl::StatusOr<Result> ExecLocked(const std::string& sql, const std::vector<std::string>& params);

  const std::string conninfo_;
  std::mutex mu_;
  pg_conn* conn_ = nullptr;
};

}  // namespace pg

#endif
