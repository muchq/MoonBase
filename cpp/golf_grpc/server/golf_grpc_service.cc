#include "cpp/golf_grpc/server/golf_grpc_service.h"

#include <grpcpp/grpcpp.h>

#include "cpp/cards/card_proto_mapper.h"
#include "cpp/futility/status/status.h"
#include "protos/golf_grpc/golf.pb.h"

using futility::status::AbseilToGrpc;
using grpc::ServerContext;
using grpc::Status;
using std::string;

GolfServiceImpl::GolfServiceImpl(std::shared_ptr<golf::GameManager> gm) : gm_(std::move(gm)) {}

Status GolfServiceImpl::RegisterUser(ServerContext* context,
                                     const golf_grpc::RegisterUserRequest* request,
                                     golf_grpc::RegisterUserResponse* response) {
  const auto status_or_username = gm_->registerUser(request->user_id());
  return AbseilToGrpc(status_or_username.status());
};

Status GolfServiceImpl::NewGame(ServerContext* context, const golf_grpc::NewGameRequest* request,
                                golf_grpc::NewGameResponse* response) {
  auto status_or_game_state = gm_->newGame(request->user_id(), request->number_of_players());
  return HandleGameStateResponse(status_or_game_state, request->user_id(),
                                 response->mutable_game_state());
}

grpc::Status GolfServiceImpl::JoinGame(grpc::ServerContext* context,
                                       const golf_grpc::JoinGameRequest* request,
                                       golf_grpc::JoinGameResponse* response) {
  auto status_or_game_state = gm_->joinGame(request->game_id(), request->user_id());
  return HandleGameStateResponse(status_or_game_state, request->user_id(),
                                 response->mutable_game_state());
};

Status GolfServiceImpl::Peek(ServerContext* context, const golf_grpc::PeekRequest* request,
                             golf_grpc::PeekResponse* response) {
  auto status_or_game_state = gm_->peekAtDrawPile(request->game_id(), request->user_id());
  auto mutable_game_state = response->mutable_game_state();

  auto status_to_return =
      HandleGameStateResponse(status_or_game_state, request->user_id(), mutable_game_state);
  if (status_to_return.ok()) {
    auto& game_state = status_or_game_state.value();
    FlipCard(mutable_game_state->mutable_top_draw(), game_state->getDrawPile());
  }
  return status_to_return;
};

Status GolfServiceImpl::DiscardDraw(ServerContext* context,
                                    const golf_grpc::DiscardDrawRequest* request,
                                    golf_grpc::DiscardDrawResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::SwapForDraw(ServerContext* context,
                                    const golf_grpc::SwapForDrawRequest* request,
                                    golf_grpc::SwapForDrawResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::SwapForDiscard(ServerContext* context,
                                       const golf_grpc::SwapForDiscardRequest* request,
                                       golf_grpc::SwapForDiscardResponse* response) {
  return Status::OK;
};

Status GolfServiceImpl::Knock(ServerContext* context, const golf_grpc::KnockRequest* request,
                              golf_grpc::KnockResponse* response) {
  return Status::OK;
};

void GolfServiceImpl::HydrateResponseGameState(const string& current_user_id,
                                               golf_grpc::GameState* response_state,
                                               const golf::GameStatePtr game_state) {
  response_state->set_all_here(game_state->allPlayersPresent());
  response_state->set_discard_size(game_state->getDiscardPile().size());
  response_state->set_draw_size(game_state->getDrawPile().size());
  response_state->set_game_id(game_state->getGameId());
  response_state->set_version(game_state->getVersionId());
  response_state->set_game_started(game_state->allPlayersPresent());
  response_state->set_game_over(game_state->isOver());

  auto knocker_index = game_state->getWhoKnocked();
  if (knocker_index > -1 && knocker_index < game_state->getPlayers().size()) {
    response_state->set_knocker(game_state->getPlayer(knocker_index).getName().value_or(""));
  }

  //   TODO: on game start, let all players peek at their bottom two cards
  //   optional VisibleHand hand = 9;

  response_state->set_number_of_players(game_state->getPlayers().size());
  auto* player_names = response_state->mutable_players();
  player_names->Clear();
  auto* player_scores = response_state->mutable_scores();
  player_scores->Clear();

  for (const auto& p : game_state->getPlayers()) {
    player_names->Add(p.getName().value_or("N/A"));
    if (game_state->isOver()) {
      player_scores->Add(p.score());
    }
  }

  // always show the top of the discard pile
  FlipCard(response_state->mutable_top_discard(), game_state->getDiscardPile());

  const auto current_player_making_call_index = game_state->playerIndex(current_user_id);
  response_state->set_your_turn(current_player_making_call_index != -1 && current_player_making_call_index == game_state->getWhoseTurn());

  // Set the new current_player_id field
  int global_current_turn_idx = game_state->getWhoseTurn();
  if (global_current_turn_idx >= 0 && global_current_turn_idx < game_state->getPlayers().size()) {
    response_state->set_current_player_id(game_state->getPlayer(global_current_turn_idx).getName().value_or(""));
  } else {
    response_state->set_current_player_id(""); // No current player or game not started
  }
}

void GolfServiceImpl::FlipCard(cards_proto::Card* response_card,
                               const std::deque<cards::Card>& deck) {
  if (!deck.empty()) {
    auto& top_of_deck = deck.back();
    response_card->set_suit(SuitToProto(top_of_deck.getSuit()));
    response_card->set_rank(RankToProto(top_of_deck.getRank()));
  }
}

Status GolfServiceImpl::HandleGameStateResponse(
    absl::StatusOr<golf::GameStatePtr> status_or_game_state, const std::string& user_id,
    golf_grpc::GameState* response_state) {
  if (!status_or_game_state.ok()) {
    return AbseilToGrpc(status_or_game_state.status());
  }

  const auto& game_state = status_or_game_state.value();

  HydrateResponseGameState(user_id, response_state, game_state);
  return Status::OK;
}
