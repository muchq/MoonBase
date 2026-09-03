#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_THOUGHTS_HUB_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_THOUGHTS_HUB_H

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

/// Thoughts on the games hub (#79): muchq.com/thoughts, one shared world in
/// which every joined player is a position on the ground plane, a color,
/// and a shape, and each change fans out to every other session. No rooms,
/// no persistence, no reconnect grace — presence is the whole game, so a
/// dropped socket is a player gone.
///
/// The world's rules are the Go server's (games_ws_backend/thoughts, kept
/// verbatim so the UI's own bounds keep matching): position is [x, 0, z]
/// with x and z within ±kWorldHalfExtent, color is three components in
/// 0..1, shape is 0, 1 or 2. A command that breaks one is refused in-band
/// (commandRejected) and changes nothing, as is move/shape before join.
/// Refused rather than swallowed, which is what the Go server did: a
/// client could not tell a rejected move from a lost one.
///
/// Fan-out reaches every live session, joined or not, minus the actor: a
/// session that has connected but not yet joined is watching the world.
///
/// Counters carry the thoughts_ prefix so golf's stream_* dashboards keep
/// their meaning; every series is declared at zero at construction (#1323)
/// and listed in DeclaredCounterSeries(), which HubHandler folds into its
/// own list so one sweep covers both hubs.
class ThoughtsHub {
 public:
  using Registry = smithy::server::SessionRegistry<moonbase::games::ThoughtsEvents>;

  static constexpr double kWorldHalfExtent = 50.0;

  /// Every counter series this hub emits, on HubHandler's terms: the
  /// thoughts_commands/thoughts_events values are the model's union cases,
  /// pinned both ways against thoughts.smithy by
  /// ThoughtsSeriesMatchTheModelUnions.
  static const std::vector<CounterSeries>& DeclaredCounterSeries();

  explicit ThoughtsHub(std::shared_ptr<TicketVault> vault,
                       std::shared_ptr<futility::otel::MetricsRecorder> metrics = nullptr,
                       ThoughtsLimits limits = {});

  /// The stream: spend the ticket, admit the seat, sessionReady, then
  /// commands until the socket closes. HubHandler::Think forwards here.
  smithy::eventstream::StreamTask Think(moonbase::games::ThinkInput input,
                                        moonbase::games::ThinkAsyncServerStream& stream);

  /// For main's SIGTERM path: Drain before the transport stops.
  Registry& registry() { return registry_; }

 private:
  void HandleCommand(const std::string& player_id,
                     const moonbase::games::ThoughtsCommands& command);
  /// Removes the player from the world and tells everyone else; false when
  /// they were not in it. Shared by the leave command and the socket close.
  bool Leave(const std::string& player_id);
  void Reject(const std::string& player_id, RejectKind kind, std::string reason);
  /// Every event leaves through one of these so thoughts_events sees each
  /// delivery.
  void Send(const std::string& player_id, moonbase::games::ThoughtsEvents event);
  void SendToOthers(const std::string& sender_id, const moonbase::games::ThoughtsEvents& event);
  void Count(const char* name, const std::map<std::string, std::string>& attributes = {});
  void TrackActive(int delta);

  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
  const ThoughtsLimits limits_;
  std::mutex mu_;
  /// The world: joined players by id. A live session without an entry
  /// here has connected and not joined (or has left).
  std::map<std::string, moonbase::games::WorldPlayer> world_;
  // Declared last: destroyed first, joining registry threads before the
  // map they could touch goes away.
  Registry registry_;
};

}  // namespace games_hub

#endif
