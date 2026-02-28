#ifndef CPP_GOLF_SERVICE_HANDLERS_H
#define CPP_GOLF_SERVICE_HANDLERS_H

#include <functional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/golf_service/game_state_mapper.h"
#include "domains/games/libs/cards/golf/game_manager.h"
#include "domains/games/protos/golf_ws/golf_ws.pb.h"
#include "mongoose.h"

namespace golf_service {
using absl::StatusOr;
using golf_ws::RequestWrapper;
using std::string;

typedef golf_ws::RequestWrapper GolfServiceRequest;
typedef std::function<void(const GolfServiceRequest&, struct ::mg_connection*)> handler;
typedef std::function<absl::StatusOr<GolfServiceRequest>(std::vector<std::string>)> argReader;
typedef absl::StatusOr<GolfServiceRequest> StatusOrRequest;

void handleDisconnect(struct ::mg_connection* c);
void handleMessage(struct ::mg_ws_message* wm, struct ::mg_connection* c);

class Handler {
 public:
  explicit Handler(std::shared_ptr<golf::GameManager> gm_) : gm(std::move(gm_)) {}
  void handleDisconnect(struct ::mg_connection* c);
  void handleMessage(struct ::mg_ws_message* wm, struct ::mg_connection* c);

 private:
  template <RequestWrapper::KindCase T>
  bool validRequestType(const GolfServiceRequest& serviceRequest, struct mg_connection* c);

  void registerUser(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  bool usernameMismatch(const string& username, struct mg_connection* c);
  StatusOr<golf::Position> validatePosition(const golf_ws::Position& position,
                                            struct mg_connection* c);
  string userStateToJson(const golf::GameStatePtr& gameStatePtr, const string& user);

  void handleGameManagerResult(const absl::StatusOr<golf::GameStatePtr>& res,
                               struct mg_connection* c);
  void newGame(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  void joinGame(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  void peekAtDrawPile(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  void discardFromDrawPile(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  void swapForDrawPile(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  void swapForDiscardPile(const GolfServiceRequest& serviceRequest, struct mg_connection* c);
  void knock(const GolfServiceRequest& serviceRequest, struct mg_connection* c);

  // TODO: make this map<string, pair<handler, parser>> ?

  typedef void (Handler::*handler_method_t)(const GolfServiceRequest&, struct ::mg_connection*);
  std::unordered_map<string, handler_method_t> handlers{
      {"register", &Handler::registerUser},
      {"new", &Handler::newGame},
      {"join", &Handler::joinGame},
      {"peek", &Handler::peekAtDrawPile},
      {"discardDraw", &Handler::discardFromDrawPile},
      {"swapDraw", &Handler::swapForDrawPile},
      {"swapDiscard", &Handler::swapForDiscardPile},
      {"knock", &Handler::knock}};

  std::shared_ptr<golf::GameManager> gm;
  golf::GameStateMapper gameStateMapper{{}};
  std::unordered_map<std::string, mg_connection*> connectionsByUser;
};

}  // namespace golf_service

#endif
