#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GAMES_HUB_HANDLER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GAMES_HUB_HANDLER_H

#include <memory>
#include <vector>

#include "domains/games/apis/games_hub/golf_hub.h"
#include "domains/games/apis/games_hub/hub_metrics.h"
#include "domains/games/apis/games_hub/id_generator.h"
#include "domains/games/apis/games_hub/thoughts_hub.h"
#include "domains/games/apis/games_hub/ticket_vault.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// The generated service's one handler (#79): session identity here, and
/// each game's stream forwarded whole to its hub. The games share the
/// vault (one ticket opens either stream) and nothing else; adding a game
/// is one forwarding override and one member.
class GamesHubHandler final : public moonbase::games::GamesHubAsyncHandler {
 public:
  /// Every counter series either hub declares — what a sweep over a
  /// recorder both hubs write to has to know. Each hub declares its own
  /// list at its own construction.
  static const std::vector<CounterSeries>& DeclaredCounterSeries();

  GamesHubHandler(std::shared_ptr<TicketVault> vault, std::shared_ptr<IdGenerator> ids,
                  std::shared_ptr<GolfHub> golf, std::shared_ptr<ThoughtsHub> thoughts);

  // Note: operation IO generates as <Op>Input/<Op>Output regardless of
  // the named shapes bound in the model, and every namespace's shapes
  // land in moonbase::games (codegen flattens the model into the one
  // namespace the BUILD rule names).
  smithy::Outcome<moonbase::games::GetSessionOutput> GetSession(
      const moonbase::games::GetSessionInput& input,
      const smithy::server::RequestContext& context) override;

  smithy::eventstream::StreamTask Play(moonbase::games::PlayInput input,
                                       moonbase::games::PlayAsyncServerStream& stream) override;

  smithy::eventstream::StreamTask Think(moonbase::games::ThinkInput input,
                                        moonbase::games::ThinkAsyncServerStream& stream) override;

 private:
  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<IdGenerator> ids_;
  const std::shared_ptr<GolfHub> golf_;
  const std::shared_ptr<ThoughtsHub> thoughts_;
};

}  // namespace games_hub

#endif
