#include "cpp/golf_service/handlers.h"

#include <google/protobuf/util/json_util.h>

#include <string>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "mongoose.h"

using golf_service::GolfServiceRequest;
using golf_ws::RegisterUserRequest;
using golf_ws::RequestWrapper;
using std::string;

namespace golf_service {

template <RequestWrapper::KindCase T>
auto Handler::validRequestType(const GolfServiceRequest& serviceRequest, struct mg_connection* c)
    -> bool {
  if (serviceRequest.kind_case() != T) {
    string output("error|invalid request");
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return false;
  }
  return true;
}

void Handler::registerUser(const GolfServiceRequest& serviceRequest, struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kRegisterUserRequest>(serviceRequest, c)) {
    return;
  }

  const RegisterUserRequest& registerUserRequest = serviceRequest.register_user_request();
  // don't allow re-registration yet
  for (auto i = connectionsByUser.begin(); i != connectionsByUser.end(); i++) {
    if (connectionsByUser.at(i->first) == c) {
      string output("error|already registered");
      mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
      return;
    }
  }

  auto res = gm->registerUser(registerUserRequest.username());
  if (!res.ok()) {
    string output("error|");
    output.append(res.status().message());
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  string user = *res;
  connectionsByUser.insert({user, c});
  string output(R"({"inGame":false,"username":")" + user + "\"}");
  mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
}

bool Handler::usernameMismatch(const string& username, struct mg_connection* c) {
  if (connectionsByUser.find(username) == connectionsByUser.end() ||
      connectionsByUser.at(username) != c) {
    string output("error|username mismatch");
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return true;
  }
  return false;
}

auto Handler::validatePosition(const golf_ws::Position& position, struct mg_connection* c)
    -> absl::StatusOr<golf::Position> {
  switch (position) {
    case golf_ws::Position::TOP_LEFT:
      return golf::Position::TopLeft;
    case golf_ws::Position::TOP_RIGHT:
      return golf::Position::TopRight;
    case golf_ws::Position::BOTTOM_LEFT:
      return golf::Position::BottomLeft;
    case golf_ws::Position::BOTTOM_RIGHT:
      return golf::Position::BottomRight;
    default:
      string output("error|invalid position");
      mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
      return absl::InvalidArgumentError("invalid position");
  }
}

string Handler::userStateToJson(const golf::GameStatePtr& gameStatePtr, const string& user) {
  const auto stateForUser = gameStateMapper.gameStateToProto(gameStatePtr, user);
  std::string userJson;
  auto status = google::protobuf::util::MessageToJsonString(stateForUser, &userJson);
  if (status.ok()) {
    return userJson;
  }
  return "UNKNOWN";
}

void Handler::handleGameManagerResult(const absl::StatusOr<golf::GameStatePtr>& res,
                                      struct mg_connection* c) {
  if (!res.ok()) {
    string output("error|");
    output.append(res.status().message());
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  const auto& gameStatePtr = *res;
  for (auto& user : gm->getUsersByGameId(gameStatePtr->getGameId())) {
    const auto userJson = userStateToJson(gameStatePtr, user);
    auto userConnection = connectionsByUser.at(user);
    mg_ws_send(userConnection, userJson.c_str(), userJson.size(), WEBSOCKET_OP_TEXT);
  }
}

void Handler::newGame(const GolfServiceRequest& serviceRequest, struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kNewGameRequest>(serviceRequest, c)) {
    return;
  }

  auto& newGameRequest = serviceRequest.new_game_request();
  if (usernameMismatch(newGameRequest.username(), c)) {
    return;
  }

  auto res = gm->newGame(newGameRequest.username(), newGameRequest.number_of_players());
  handleGameManagerResult(res, c);
}

void Handler::joinGame(const GolfServiceRequest& serviceRequest, struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kJoinGameRequest>(serviceRequest, c)) {
    return;
  }

  auto& joinGameRequest = serviceRequest.join_game_request();
  if (usernameMismatch(joinGameRequest.username(), c)) {
    return;
  }
  auto res = gm->joinGame(joinGameRequest.game_id(), joinGameRequest.username());
  handleGameManagerResult(res, c);
}

void Handler::peekAtDrawPile(const GolfServiceRequest& serviceRequest, struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kPeekRequest>(serviceRequest, c)) {
    return;
  }

  auto& peekRequest = serviceRequest.peek_request();
  if (usernameMismatch(peekRequest.username(), c)) {
    return;
  }
  auto res = gm->peekAtDrawPile(peekRequest.game_id(), peekRequest.username());
  handleGameManagerResult(res, c);
}

void Handler::discardFromDrawPile(const GolfServiceRequest& serviceRequest,
                                  struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kDiscardDrawRequest>(serviceRequest, c)) {
    return;
  }

  auto& discardDrawRequest = serviceRequest.discard_draw_request();
  if (usernameMismatch(discardDrawRequest.username(), c)) {
    return;
  }
  auto res =
      gm->swapDrawForDiscardPile(discardDrawRequest.game_id(), discardDrawRequest.username());
  handleGameManagerResult(res, c);
}

void Handler::swapForDrawPile(const GolfServiceRequest& serviceRequest, struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kSwapForDrawRequest>(serviceRequest, c)) {
    return;
  }

  auto& swapForDrawRequest = serviceRequest.swap_for_draw_request();
  if (usernameMismatch(swapForDrawRequest.username(), c)) {
    return;
  }

  auto positionRes = validatePosition(swapForDrawRequest.position(), c);
  if (!positionRes.ok()) {
    return;
  }

  auto res = gm->swapForDrawPile(swapForDrawRequest.game_id(), swapForDrawRequest.username(),
                                 *positionRes);
  handleGameManagerResult(res, c);
}

void Handler::swapForDiscardPile(const GolfServiceRequest& serviceRequest,
                                 struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kSwapForDiscardRequest>(serviceRequest, c)) {
    return;
  }

  auto& swapForDiscardRequest = serviceRequest.swap_for_discard_request();
  if (usernameMismatch(swapForDiscardRequest.username(), c)) {
    return;
  }
  auto positionRes = validatePosition(swapForDiscardRequest.position(), c);
  if (!positionRes.ok()) {
    return;
  }
  auto res = gm->swapForDiscardPile(swapForDiscardRequest.game_id(),
                                    swapForDiscardRequest.username(), *positionRes);
  handleGameManagerResult(res, c);
}

void Handler::knock(const GolfServiceRequest& serviceRequest, struct mg_connection* c) {
  if (!validRequestType<RequestWrapper::KindCase::kKnockRequest>(serviceRequest, c)) {
    return;
  }

  auto& knockRequest = serviceRequest.knock_request();
  if (usernameMismatch(knockRequest.username(), c)) {
    return;
  }
  auto res = gm->knock(knockRequest.game_id(), knockRequest.username());
  handleGameManagerResult(res, c);
}

void Handler::handleMessage(struct mg_ws_message* wm, struct mg_connection* c) {
  const string requestText(wm->data.buf);
  golf_ws::RequestWrapper requestWrapper;
  auto status = google::protobuf::util::JsonStringToMessage(requestText, &requestWrapper);
  if (!status.ok()) {
    auto messageString = std::string{status.message()};
    mg_ws_send(c, messageString.c_str(), messageString.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  auto command = handlers.find(requestWrapper.command());
  if (command == handlers.end()) {
    std::string response = "error|bad_command";
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  auto handler = command->second;
  (this->*(handler))(requestWrapper, c);
}

void Handler::handleDisconnect(struct ::mg_connection* c) {
  // TODO: unregister connection
  // TODO: notify players in shared games
}
}  // namespace golf_service
