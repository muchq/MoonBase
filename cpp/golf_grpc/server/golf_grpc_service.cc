#include "cpp/golf_grpc/server/golf_grpc_service.h"

#include <grpcpp/grpcpp.h>

#include "cpp/futility/status/status.h"
#include "protos/golf_grpc/golf.pb.h"

using futility::status::AbseilToGrpc;
using grpc::ServerContext;
using grpc::Status;
using std::string;

GolfServiceImpl::GolfServiceImpl(golf::GameManager gm) : gm_(std::move(gm)) {}

Status GolfServiceImpl::RegisterUser(ServerContext* context,
                                     const golf_grpc::RegisterUserRequest* request,
                                     golf_grpc::RegisterUserResponse* response) {
  auto res = gm_.registerUser(request->user_id());
  return AbseilToGrpc(res.status());
};

Status GolfServiceImpl::NewGame(ServerContext* context, const golf_grpc::NewGameRequest* request,
                                golf_grpc::NewGameResponse* response) {
  auto res = gm_.newGame(request->user_id(), request->number_of_players());
  if (!res.ok()) {
    return AbseilToGrpc(res.status());
  }

  HydrateResponseGameState(request->user_id(), response->mutable_game_state(), res->get());
  return Status::OK;
};

Status GolfServiceImpl::Peek(ServerContext* context, const golf_grpc::PeekRequest* request,
                             golf_grpc::PeekResponse* response) {
  return Status::OK;
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
                                               const golf::GameState* game_state) {
  response_state->set_all_here(game_state->allPlayersPresent());
  response_state->set_discard_size(game_state->getDiscardPile().size());
  response_state->set_draw_size(game_state->getDrawPile().size());
  response_state->set_game_id(game_state->getGameId());
  response_state->set_version(game_state->getVersionId());
  response_state->set_game_started(game_state->allPlayersPresent());
  response_state->set_game_over(game_state->isOver());

  auto knocker_index = game_state->getWhoKnocked();
  if (knocker_index > -1) {
    response_state->set_knocker(game_state->getPlayer(knocker_index).getName().value());
  }

  //   TODO: on game start, let all players peek at their bottom two cards
  //   optional VisibleHand hand = 9;

  response_state->set_number_of_players(game_state->getPlayers().size());
  auto player_names = response_state->mutable_players();
  auto player_scores = response_state->mutable_scores();
  for (auto& p : game_state->getPlayers()) {
    player_names->Add(p.getName().value_or("N/A"));
    player_scores->Add(p.score());
  }

  // if (!game_state->getDiscardPile().empty()) {
  //   auto response_top_discard = response_state->mutable_top_discard();
  //   auto& top_discard = game_state->getDiscardPile().back();
  //   top_discard->set_suit(game_state->getDiscardPile().back());
  // }
  // response_state->set_top_draw(game_state->getDrawPile().back());

  response_state->set_your_turn(game_state->playerIndex(current_user_id) ==
                                game_state->getWhoseTurn());
}
