#include "domains/platform/libs/pg/pg.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "libpq-fe.h"

namespace pg {
namespace {

std::string TrimmedError(const char* message) {
  std::string error = message == nullptr ? "" : message;
  while (!error.empty() && (error.back() == '\n' || error.back() == ' ')) {
    error.pop_back();
  }
  return error.empty() ? "unknown postgres error" : error;
}

}  // namespace

Result::Result(pg_result* result) : result_(result) {}

Result::~Result() {
  if (result_ != nullptr) PQclear(result_);
}

Result::Result(Result&& other) noexcept : result_(std::exchange(other.result_, nullptr)) {}

Result& Result::operator=(Result&& other) noexcept {
  if (this != &other) {
    if (result_ != nullptr) PQclear(result_);
    result_ = std::exchange(other.result_, nullptr);
  }
  return *this;
}

int Result::rows() const { return result_ == nullptr ? 0 : PQntuples(result_); }

int Result::columns() const { return result_ == nullptr ? 0 : PQnfields(result_); }

std::optional<std::string> Result::Get(int row, int column) const {
  if (result_ == nullptr || row < 0 || row >= rows() || column < 0 || column >= columns()) {
    return std::nullopt;
  }
  if (PQgetisnull(result_, row, column) != 0) return std::nullopt;
  return std::string(PQgetvalue(result_, row, column));
}

Client::Client(std::string conninfo) : conninfo_(std::move(conninfo)) {}

Client::~Client() {
  if (conn_ != nullptr) PQfinish(conn_);
}

absl::Status Client::EnsureConnectedLocked() {
  if (conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK) return absl::OkStatus();
  if (conn_ != nullptr) {
    PQfinish(conn_);
    conn_ = nullptr;
  }
  conn_ = PQconnectdb(conninfo_.c_str());
  if (conn_ == nullptr || PQstatus(conn_) != CONNECTION_OK) {
    const std::string error = TrimmedError(conn_ == nullptr ? nullptr : PQerrorMessage(conn_));
    if (conn_ != nullptr) {
      PQfinish(conn_);
      conn_ = nullptr;
    }
    return absl::UnavailableError(absl::StrCat("postgres connect failed: ", error));
  }
  return absl::OkStatus();
}

absl::StatusOr<Result> Client::ExecLocked(const std::string& sql,
                                          const std::vector<std::string>& params) {
  std::vector<const char*> values;
  values.reserve(params.size());
  for (const std::string& param : params) values.push_back(param.c_str());
  pg_result* raw =
      PQexecParams(conn_, sql.c_str(), static_cast<int>(values.size()),
                   /*paramTypes=*/nullptr, values.data(),
                   /*paramLengths=*/nullptr, /*paramFormats=*/nullptr, /*resultFormat=*/0);
  Result result(raw);
  const ExecStatusType status = raw == nullptr ? PGRES_FATAL_ERROR : PQresultStatus(raw);
  if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) return result;
  return absl::InternalError(absl::StrCat(
      "postgres statement failed: ",
      TrimmedError(raw == nullptr ? PQerrorMessage(conn_) : PQresultErrorMessage(raw))));
}

absl::StatusOr<Result> Client::Exec(const std::string& sql,
                                    const std::vector<std::string>& params) {
  const std::lock_guard<std::mutex> lock(mu_);
  if (absl::Status connected = EnsureConnectedLocked(); !connected.ok()) return connected;
  absl::StatusOr<Result> result = ExecLocked(sql, params);
  // A statement-level failure on a healthy connection is the caller's
  // problem; a dead connection gets one reconnect + retry so a postgres
  // restart heals here instead of surfacing.
  if (result.ok() || PQstatus(conn_) == CONNECTION_OK) return result;
  if (absl::Status connected = EnsureConnectedLocked(); !connected.ok()) return connected;
  return ExecLocked(sql, params);
}

}  // namespace pg
