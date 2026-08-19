#include "domains/games/apis/one_d4_worker/db_options.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace one_d4_worker {

std::string WithExecutionBounds(std::string_view url) {
  static_assert(kSocketTimeoutSeconds > kStatementTimeoutSeconds,
                "the transport bound must outlast the statement bound, or a healthy slow "
                "statement is severed before the server can cancel it cleanly");

  // keepalives so the transport has something to time out on while a
  // statement is running silently; tcp_user_timeout alone measures
  // unacknowledged data, and a connection waiting on a lock sends none.
  return absl::StrCat(
      url, url.find('?') == std::string_view::npos ? "?" : "&", "options=-c%20statement_timeout%3D",
      kStatementTimeoutSeconds * 1000, "&tcp_user_timeout=", kSocketTimeoutSeconds * 1000,
      "&connect_timeout=", kConnectTimeoutSeconds, "&keepalives=1&keepalives_idle=30");
}

}  // namespace one_d4_worker
