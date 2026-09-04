#include "domains/games/apis/games_hub/games_hub_handler.h"

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/protocol_input.h"
#include "smithy/core/error.h"

namespace games_hub {

GamesHubHandler::GamesHubHandler(std::shared_ptr<TicketVault> vault,
                                 std::shared_ptr<IdGenerator> ids, std::shared_ptr<GolfHub> golf,
                                 std::shared_ptr<ThoughtsHub> thoughts)
    : vault_(std::move(vault)),
      ids_(std::move(ids)),
      golf_(std::move(golf)),
      thoughts_(std::move(thoughts)) {}

const std::vector<CounterSeries>& GamesHubHandler::DeclaredCounterSeries() {
  static const auto* kSeries = [] {
    auto* series = new std::vector<CounterSeries>(GolfHub::DeclaredCounterSeries());
    const auto& thoughts = ThoughtsHub::DeclaredCounterSeries();
    series->insert(series->end(), thoughts.begin(), thoughts.end());
    return series;
  }();
  return *kSeries;
}

smithy::Outcome<moonbase::games::GetSessionOutput> GamesHubHandler::GetSession(
    const moonbase::games::GetSessionInput& input,
    const smithy::server::RequestContext& /*context*/) {
  std::string player_id;
  bool token_valid = false;
  if (input.resumeToken.has_value() && !HasEmbeddedNul(*input.resumeToken)) {
    if (auto resolved = vault_->ResolveResumeToken(*input.resumeToken)) {
      player_id = std::move(*resolved);
      token_valid = true;
    }
  }
  if (player_id.empty()) player_id = ids_->PlayerId();

  // A vault backed by a store can be down; a mint nothing recorded must
  // not reach the client. Unknown -> a non-leaking 500.
  absl::StatusOr<std::string> ticket = vault_->IssueTicket(player_id);
  if (!ticket.ok()) return smithy::Error::Unknown("credential store unavailable");

  moonbase::games::GetSessionOutput output;
  output.playerId = player_id;
  output.ticket = *std::move(ticket);
  if (token_valid) {
    output.resumeToken = *input.resumeToken;
  } else {
    absl::StatusOr<std::string> resume = vault_->IssueResumeToken(player_id);
    if (!resume.ok()) return smithy::Error::Unknown("credential store unavailable");
    output.resumeToken = *std::move(resume);
  }
  return output;
}

smithy::eventstream::StreamTask GamesHubHandler::Play(
    moonbase::games::PlayInput input, moonbase::games::PlayAsyncServerStream& stream) {
  return golf_->Play(std::move(input), stream);
}

smithy::eventstream::StreamTask GamesHubHandler::Think(
    moonbase::games::ThinkInput input, moonbase::games::ThinkAsyncServerStream& stream) {
  return thoughts_->Think(std::move(input), stream);
}

}  // namespace games_hub
