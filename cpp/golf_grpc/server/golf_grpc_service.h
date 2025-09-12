#ifndef CPP_GOLF_GRPC_SERVER_GOLF_GRPC_SERVICE_H
#define CPP_GOLF_GRPC_SERVER_GOLF_GRPC_SERVICE_H

#include "absl/status/statusor.h"
#include "cpp/cards/golf/game_manager.h"
#include "protos/golf_grpc/golf.grpc.pb.h"

class GolfServiceImpl final : public golf_grpc::Golf::Service {
 public:
  explicit GolfServiceImpl(std::shared_ptr<golf::GameManager> gm);

  grpc::Status RegisterUser(grpc::ServerContext* context,
                            const golf_grpc::RegisterUserRequest* request,
                            golf_grpc::RegisterUserResponse* response) override;
  grpc::Status NewGame(grpc::ServerContext* context, const golf_grpc::NewGameRequest* request,
                       golf_grpc::NewGameResponse* response) override;
  grpc::Status JoinGame(grpc::ServerContext* context, const golf_grpc::JoinGameRequest* request,
                        golf_grpc::JoinGameResponse* response) override;
  grpc::Status Peek(grpc::ServerContext* context, const golf_grpc::PeekRequest* request,
                    golf_grpc::PeekResponse* response) override;
  grpc::Status DiscardDraw(grpc::ServerContext* context,
                           const golf_grpc::DiscardDrawRequest* request,
                           golf_grpc::DiscardDrawResponse* response) override;
  grpc::Status SwapForDraw(grpc::ServerContext* context,
                           const golf_grpc::SwapForDrawRequest* request,
                           golf_grpc::SwapForDrawResponse* response) override;
  grpc::Status SwapForDiscard(grpc::ServerContext* context,
                              const golf_grpc::SwapForDiscardRequest* request,
                              golf_grpc::SwapForDiscardResponse* response) override;
  grpc::Status Knock(grpc::ServerContext* context, const golf_grpc::KnockRequest* request,
                     golf_grpc::KnockResponse* response) override;
  grpc::Status GetGame(grpc::ServerContext* context, const golf_grpc::GetGameRequest* request,
                       golf_grpc::GetGameResponse* response) override;

 private:
  void HydrateResponseGameState(const std::string& current_user_id,
                                golf_grpc::GameState* response_state,
                                golf::GameStatePtr game_state);
  void FlipCard(cards_proto::Card* response_card, const std::deque<cards::Card>& deck);
  grpc::Status HandleGameStateResponse(absl::StatusOr<golf::GameStatePtr> status_or_game_state,
                                       const std::string& user_id,
                                       golf_grpc::GameState* response_state);

  std::shared_ptr<golf::GameManager> gm_;
};

#endif
