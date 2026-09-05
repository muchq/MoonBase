#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_THOUGHTS_HUB_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_THOUGHTS_HUB_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "domains/games/apis/games_hub/hub_metrics.h"
#include "domains/games/apis/games_hub/ticket_vault.h"
#include "domains/games/apis/games_hub/world.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "moonbase/games/server.h"
#include "smithy/eventstream/event_stream.h"
#include "smithy/server/session_registry.h"

namespace games_hub {

/// The Think stream's budget (#1240's shape, thoughts' numbers): every
/// inbound frame draws one token. Presence updates arrive per animation
/// frame, so the bucket admits a client streaming at 60 Hz with a burst
/// for the catch-up after a stall, and refuses the flood a scripted client
/// would otherwise fan out to every other session for free. Injectable so
/// tests pin the refusal with tiny frozen buckets.
struct ThoughtsLimits {
  double command_burst = 120;
  double command_refill_per_sec = 60;
};

/// Scheduling seams for the race suites, empty in production. Each runs on
/// the acting session's own thread at the one point whose ordering a test
/// pins, and must not call into the hub — the first runs under the world
/// lock.
struct ThoughtsTestHooks {
  /// Under the world lock, after the joiner's worldState is queued and the
  /// joiner is in the world, before the lock is released.
  std::function<void()> after_snapshot_queued;
  /// On the close path, after the world entry is gone and playerLeft has
  /// fanned out, before the seat is released to a reconnect.
  std::function<void(const std::string& player_id)> before_seat_release;
};

/// Thoughts on the Think stream (#79): the pre-lobby route today's
/// muchq.com/thoughts dials, one World of its own on one registry with
/// no persistence and no reconnect grace — presence is the whole game,
/// so a dropped socket is a player gone. A join names its room or lands
/// in the plaza; any id names a world of its own, since no room layer
/// is behind this stream. The lobby member of the room stream (#1490)
/// is the same World hosted by GolfHub; this hub retires once the site
/// is on it.
///
/// Every fan-out is queued to its recipients under the world lock, in
/// the same hold as the mutation it announces, so each session's events
/// arrive in the order the world changed: a player who respawns
/// elsewhere hears the last of the old world before their own new
/// snapshot, never after it, and a snapshot is a full replacement.
///
/// Series carry the thoughts_ prefix (golf's carry golf_), so the two
/// hubs never share a name and a dashboard tile means one game. Every
/// series is declared at zero at construction (#1323) and listed in
/// DeclaredCounterSeries(), which GamesHubHandler folds together with
/// golf's so one sweep covers both hubs.
class ThoughtsHub {
 public:
  using Registry = smithy::server::SessionRegistry<moonbase::games::ThoughtsEvents>;

  /// Every counter series this hub emits, on GolfHub's terms: the
  /// thoughts_commands/thoughts_events values are the model's union cases,
  /// pinned both ways against thoughts.smithy by
  /// ThoughtsSeriesMatchTheModelUnions.
  static const std::vector<CounterSeries>& DeclaredCounterSeries();

  explicit ThoughtsHub(std::shared_ptr<TicketVault> vault,
                       std::shared_ptr<futility::otel::MetricsRecorder> metrics = nullptr,
                       ThoughtsLimits limits = {}, ThoughtsTestHooks hooks = {});

  /// The stream: spend the ticket, admit the seat, sessionReady, then
  /// commands until the socket closes; GamesHubHandler::Think forwards here.
  smithy::eventstream::StreamTask Think(moonbase::games::ThinkInput input,
                                        moonbase::games::ThinkAsyncServerStream& stream);

  /// For main's SIGTERM path: Drain before the transport stops.
  Registry& registry() { return registry_; }

 private:
  void HandleCommand(const std::string& player_id,
                     const moonbase::games::ThoughtsCommands& command);
  /// Removes the player from their world and tells the rest of it; false
  /// when they were in none. Shared by the leave command and the socket
  /// close.
  bool Leave(const std::string& player_id);
  void Reject(const std::string& player_id, RejectKind kind, std::string reason);
  /// Every event leaves through here so thoughts_events sees each
  /// delivery. The world's staged deliveries go out under mu_, as the
  /// class comment requires; the registry only enqueues, so the hold
  /// stays short, and Beast completes a slow session's close on its io
  /// thread, never inside the initiating call, so the closed session's
  /// own Leave takes mu_ after this hold rather than inside it.
  void Send(const std::string& player_id, moonbase::games::ThoughtsEvents event);
  void SendLocked(World::Deliveries& deliveries);
  void Count(const char* name, const std::map<std::string, std::string>& attributes = {});
  void TrackActive(int delta);

  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const ThoughtsLimits limits_;
  const ThoughtsTestHooks hooks_;
  std::mutex mu_;
  World world_;
  // Declared last: destroyed first, so no session it still holds can reach
  // the world above during teardown.
  Registry registry_;
};

}  // namespace games_hub

#endif
