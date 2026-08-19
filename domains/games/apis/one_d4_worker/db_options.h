#ifndef DOMAINS_GAMES_APIS_ONE_D4_WORKER_DB_OPTIONS_H
#define DOMAINS_GAMES_APIS_ONE_D4_WORKER_DB_OPTIONS_H

#include <string>
#include <string_view>

namespace one_d4_worker {

/// How long a single statement may run before the server cancels it.
///
/// Nothing else bounds this worker's database calls. A statement wedged on
/// a lock wait blocks the only poller thread the process has, and the run
/// ceiling cannot help: a thread inside libpq never reaches a checkpoint
/// to be told its time is up. Generous next to the statements this worker
/// actually runs — the longest is a hundred-game flush, and each statement
/// in it is small — so reaching it means waiting on something, not doing
/// work.
inline constexpr int kStatementTimeoutSeconds = 120;

/// How long the transport may hear nothing before tearing the connection
/// down, for the failure a server-side timeout cannot reach: a connection
/// black-holed by a network that never answers.
///
/// Must exceed the statement bound. Below it, a healthy slow statement —
/// which sends nothing until it finishes — is severed by the transport
/// before the server cancels it cleanly, turning a bounded wait into a
/// dropped connection. DataSourceFactory keeps the same ordering for the
/// same reason.
inline constexpr int kSocketTimeoutSeconds = 150;

/// How long establishing a connection may take.
///
/// pg::Client connects lazily and reconnects after a connection-level
/// failure, so this is not a startup-only concern: a black-holed
/// reconnect blocks whichever call triggered it, and libpq's default is
/// to wait forever. The call it most often blocks is the claim, which
/// this worker makes every few seconds.
inline constexpr int kConnectTimeoutSeconds = 10;

/// `url` with those bounds applied.
///
/// Carried in the conninfo rather than set with a statement, because
/// pg::Client reconnects lazily and a reconnect drops anything a session
/// SET established — silently, and back to unbounded.
std::string WithExecutionBounds(std::string_view url);

}  // namespace one_d4_worker

#endif  // DOMAINS_GAMES_APIS_ONE_D4_WORKER_DB_OPTIONS_H
