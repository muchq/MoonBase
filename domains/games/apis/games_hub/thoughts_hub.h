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

/// Thoughts on the games hub (#79): muchq.com/thoughts, a world per room
/// (#1490) in which every joined player is a position on the ground plane,
/// a color, and a shape, and each change fans out to everyone else in the
/// same world. No persistence, no reconnect grace — presence is the whole
/// game, so a dropped socket is a player gone.
///
/// A join names its room or lands in the plaza, kPlaza, a well-known room
/// nobody creates. Any other id names a world of its own; the room layer
/// is not consulted (GolfHub's rooms and this hub's worlds meet in #1490's
/// phase 3). A world exists while someone stands in it.
///
/// The world's rules match the muchq.com/thoughts UI's own bounds, so
/// retune them together: position is [x, 0, z] with x and z within
/// ±kWorldHalfExtent, color is three components in 0..1, shape is 0, 1
/// or 2. A command that breaks one is refused in-band (commandRejected)
/// and changes nothing, as is move/shape before join. Refused rather than
/// swallowed, so a client can tell a rejected move from a lost one.
///
/// Fan-out reaches the actor's world and nothing outside it: a session
/// that has connected but not joined hears nothing, and the actor never
/// hears its own echo.
///
/// Series carry the thoughts_ prefix (golf's carry golf_), so the two
/// hubs never share a name and a dashboard tile means one game. Every
/// series is declared at zero at construction (#1323) and listed in
/// DeclaredCounterSeries(), which GamesHubHandler folds together with
/// golf's so one sweep covers both hubs.
class ThoughtsHub {
 public:
  using Registry = smithy::server::SessionRegistry<moonbase::games::ThoughtsEvents>;

  static constexpr double kWorldHalfExtent = 50.0;
  /// The unroomed join's world. Lowercase, so no generated room code
  /// (IdGenerator's uppercase alphanumerics) can name it.
  static constexpr const char* kPlaza = "plaza";

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
  /// Everyone else in `room_id`'s world. Under mu_, so a recipient set is
  /// the world as of the mutation it announces.
  std::vector<std::string> OthersInLocked(const std::string& room_id,
                                          const std::string& player_id) const;
  /// Every event leaves through one of these so thoughts_events sees each
  /// delivery. Broadcasts go outside mu_: a fan-out can close a slow
  /// session, and on the in-memory wire that completes its Receive — and
  /// so its Leave — inline on this thread.
  void Send(const std::string& player_id, moonbase::games::ThoughtsEvents event);
  void Broadcast(const std::vector<std::string>& recipients,
                 const moonbase::games::ThoughtsEvents& event);
  void Count(const char* name, const std::map<std::string, std::string>& attributes = {});
  void TrackActive(int delta);

  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const ThoughtsLimits limits_;
  const ThoughtsTestHooks hooks_;
  struct Standing {
    std::string room_id;
    moonbase::games::WorldPlayer player;
  };
  std::mutex mu_;
  /// Every joined player by id, with the room whose world they stand in.
  /// A live session without an entry here has connected and not joined
  /// (or has left). One map rather than one per world: a fan-out walks
  /// every joined player either way, and there is no world to forget
  /// when its last player leaves.
  std::map<std::string, Standing> world_;
  // Declared last: destroyed first, so no session it still holds can reach
  // the map above during teardown.
  Registry registry_;
};

}  // namespace games_hub

#endif
