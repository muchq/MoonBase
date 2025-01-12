#include "cpp/golf_grpc/server/golf_grpc_service.h"

#include <grpcpp/grpcpp.h>

#include "cpp/cards/card_proto_mapper.h"
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
  const auto status_or_username = gm_.registerUser(request->user_id());
  return AbseilToGrpc(status_or_username.status());
};

Status GolfServiceImpl::NewGame(ServerContext* context, const golf_grpc::NewGameRequest* request,
                                golf_grpc::NewGameResponse* response) {
  auto status_or_game_state = gm_.newGame(request->user_id(), request->number_of_players());
  if (!status_or_game_state.ok()) {
    return AbseilToGrpc(status_or_game_state.status());
  }

  auto game_state = status_or_game_state.value();

  HydrateResponseGameState(request->user_id(), response->mutable_game_state(), game_state);
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
                                               const golf::GameStatePtr game_state) {
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

  if (!game_state->getDiscardPile().empty()) {
    auto response_top_discard = response_state->mutable_top_discard();
    auto& top_discard = game_state->getDiscardPile().back();
    response_top_discard->set_suit(cards::SuitToProto(top_discard.getSuit()));
    response_top_discard->set_rank(cards::RankToProto(top_discard.getRank()));
  }

  if (!game_state->getDrawPile().empty()) {
    auto response_top_draw = response_state->mutable_top_draw();
    auto& top_draw = game_state->getDrawPile().back();
    response_top_draw->set_suit(cards::SuitToProto(top_draw.getSuit()));
    response_top_draw->set_rank(cards::RankToProto(top_draw.getRank()));
  }

  response_state->set_your_turn(game_state->playerIndex(current_user_id) ==
                                game_state->getWhoseTurn());
}
