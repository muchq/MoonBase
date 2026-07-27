#ifndef DOMAINS_PLATFORM_LIBS_PG_PG_H
#define DOMAINS_PLATFORM_LIBS_PG_PG_H

#include <functional>
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

class Client;

/// Statement access inside Client::InTransaction. Every call runs on the
/// connection the transaction owns, in order, with no reconnect and no
/// retry: re-running a statement after BEGIN would execute it against a
/// transaction the server has already aborted.
/// The first failed Exec poisons the transaction; later calls return the
/// same failure, and InTransaction cannot report success even if the
/// callback accidentally ignores it.
///
/// Each statement takes its own snapshot, which is the reason to reach
/// for this over a CTE-chained single statement. A statement that waits
/// on a row lock still reads the snapshot it took before waiting, so
/// anything the lock holder commits meanwhile stays invisible to it. Do
/// the locking in one statement and the work that must see current rows
/// in the next.
class Transaction {
 public:
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  absl::StatusOr<Result> Exec(const std::string& sql, const std::vector<std::string>& params = {});

 private:
  friend class Client;
  explicit Transaction(Client& client) : client_(client) {}

  Client& client_;
  absl::Status failure_;
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

  /// Runs `body` between BEGIN and COMMIT, holding the connection for
  /// the whole callback so no other caller can interleave a statement.
  /// A non-ok return from `body`, a failed transaction statement, or a
  /// failed BEGIN/COMMIT rolls back.
  ///
  /// Nothing is retried: a transaction whose COMMIT was sent but never
  /// acknowledged may or may not have landed, and re-running it would
  /// double any write it contains. Callers that need a result out of
  /// the callback capture it by reference.
  ///
  /// `body` must not touch this Client — Exec would deadlock on the
  /// connection lock this call already holds. Use the Transaction.
  absl::Status InTransaction(const std::function<absl::Status(Transaction&)>& body);

 private:
  friend class Transaction;

  absl::Status EnsureConnectedLocked();
  absl::StatusOr<Result> ExecLocked(const std::string& sql, const std::vector<std::string>& params);

  const std::string conninfo_;
  std::mutex mu_;
  pg_conn* conn_ = nullptr;
};

}  // namespace pg

#endif
