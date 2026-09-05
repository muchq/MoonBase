#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_GAMES_HUB_HANDLER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_GAMES_HUB_HANDLER_H

#include <memory>

#include "domains/games/apis/games_hub/golf_hub.h"
#include "domains/games/apis/games_hub/id_generator.h"
#include "domains/games/apis/games_hub/ticket_vault.h"
#include "moonbase/games/server.h"

namespace games_hub {

/// The generated service's one handler (#79): session identity here, and
/// the one stream, Play, forwarded whole to the room hub.
///
/// A new game is one more envelope member on Play — castle (#77) and the
/// lobby (#1490) ride GolfHub's room layer (rooms, chat, grace, the store,
/// per-viewer redaction) that way — so the next game extends the room
/// host, and the README says which union shape it copies.
class GamesHubHandler final : public moonbase::games::GamesHubAsyncHandler {
 public:
  GamesHubHandler(std::shared_ptr<TicketVault> vault, std::shared_ptr<IdGenerator> ids,
                  std::shared_ptr<GolfHub> golf);

  // Note: operation IO generates as <Op>Input/<Op>Output regardless of
  // the named shapes bound in the model, and every namespace's shapes
  // land in moonbase::games (codegen flattens the model into the one
  // namespace the BUILD rule names).
  smithy::Outcome<moonbase::games::GetSessionOutput> GetSession(
      const moonbase::games::GetSessionInput& input,
      const smithy::server::RequestContext& context) override;

  smithy::eventstream::StreamTask Play(moonbase::games::PlayInput input,
                                       moonbase::games::PlayAsyncServerStream& stream) override;

 private:
  const std::shared_ptr<TicketVault> vault_;
  const std::shared_ptr<IdGenerator> ids_;
  const std::shared_ptr<GolfHub> golf_;
};

}  // namespace games_hub

#endif
