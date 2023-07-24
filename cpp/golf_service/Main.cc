#include <google/protobuf/util/json_util.h>

#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "cpp/cards/golf/golf.h"
#include "cpp/golf_service/api.h"
#include "cpp/golf_service/game_state_mapper.h"
#include "mongoose.h"
#include "protos/golf_ws/golf_ws.pb.h"

using golf_service::GolfServiceRequest;
using golf_ws::RegisterUserRequest;
using golf_ws::RequestWrapper;
using std::string;

std::mutex m;
std::unordered_map<std::string, mg_connection *> connectionsByUser;
golf::GameManager gm;
golf::GameStateMapper gameStateMapper{{}};

template <RequestWrapper::KindCase T>
static auto validRequestType(const GolfServiceRequest &serviceRequest, struct mg_connection *c)
    -> bool {
  if (serviceRequest.kind_case() != T) {
    string output("error|invalid request");
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return false;
  }
  return true;
}

static void registerUser(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kRegisterUserRequest>(serviceRequest, c)) {
    return;
  }

  const RegisterUserRequest &registerUserRequest = serviceRequest.register_user_request();
  // don't allow re-registration yet
  for (auto i = connectionsByUser.begin(); i != connectionsByUser.end(); i++) {
    if (connectionsByUser.at(i->first) == c) {
      string output("error|already registered");
      mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
      return;
    }
  }

  auto res = gm.registerUser(registerUserRequest.username());
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

static bool usernameMismatch(const string &username, struct mg_connection *c) {
  if (connectionsByUser.find(username) == connectionsByUser.end() ||
      connectionsByUser.at(username) != c) {
    string output("error|username mismatch");
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return true;
  }
  return false;
}

static auto validatePosition(const golf_ws::Position &position, struct mg_connection *c)
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

static auto userStateToJson(const golf::GameStatePtr &gameStatePtr, const string &user) -> string {
  const auto stateForUser = gameStateMapper.gameStateToProto(gameStatePtr, user);
  std::string userJson;
  google::protobuf::util::MessageToJsonString(stateForUser, &userJson);
  return userJson;
}

static void handleGameManagerResult(const absl::StatusOr<golf::GameStatePtr> &res,
                                    struct mg_connection *c) {
  if (!res.ok()) {
    string output("error|");
    output.append(res.status().message());
    mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  const auto &gameStatePtr = *res;
  for (auto &user : gm.getUsersByGameId(gameStatePtr->getGameId())) {
    const auto userJson = userStateToJson(gameStatePtr, user);
    auto userConnection = connectionsByUser.at(user);
    mg_ws_send(userConnection, userJson.c_str(), userJson.size(), WEBSOCKET_OP_TEXT);
  }
}

static void newGame(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kNewGameRequest>(serviceRequest, c)) {
    return;
  }

  auto &newGameRequest = serviceRequest.new_game_request();
  if (usernameMismatch(newGameRequest.username(), c)) {
    return;
  }

  auto res = gm.newGame(newGameRequest.username(), newGameRequest.number_of_players());
  handleGameManagerResult(res, c);
}

static void joinGame(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kJoinGameRequest>(serviceRequest, c)) {
    return;
  }

  auto &joinGameRequest = serviceRequest.join_game_request();
  if (usernameMismatch(joinGameRequest.username(), c)) {
    return;
  }
  auto res = gm.joinGame(joinGameRequest.game_id(), joinGameRequest.username());
  handleGameManagerResult(res, c);
}

static void peekAtDrawPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kPeekRequest>(serviceRequest, c)) {
    return;
  }

  auto &peekRequest = serviceRequest.peek_request();
  if (usernameMismatch(peekRequest.username(), c)) {
    return;
  }
  auto res = gm.peekAtDrawPile(peekRequest.username());
  handleGameManagerResult(res, c);
}

static void discardFromDrawPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kDiscardDrawRequest>(serviceRequest, c)) {
    return;
  }

  auto &discardDrawRequest = serviceRequest.discard_draw_request();
  if (usernameMismatch(discardDrawRequest.username(), c)) {
    return;
  }
  auto res = gm.swapDrawForDiscardPile(discardDrawRequest.username());
  handleGameManagerResult(res, c);
}

static void swapForDrawPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kSwapForDrawRequest>(serviceRequest, c)) {
    return;
  }

  auto &swapForDrawRequest = serviceRequest.swap_for_draw_request();
  if (usernameMismatch(swapForDrawRequest.username(), c)) {
    return;
  }

  auto positionRes = validatePosition(swapForDrawRequest.position(), c);
  if (!positionRes.ok()) {
    return;
  }

  auto res = gm.swapForDrawPile(swapForDrawRequest.username(), *positionRes);
  handleGameManagerResult(res, c);
}

static void swapForDiscardPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kSwapForDiscardRequest>(serviceRequest, c)) {
    return;
  }

  auto &swapForDiscardRequest = serviceRequest.swap_for_discard_request();
  if (usernameMismatch(swapForDiscardRequest.username(), c)) {
    return;
  }
  auto positionRes = validatePosition(swapForDiscardRequest.position(), c);
  if (!positionRes.ok()) {
    return;
  }
  auto res = gm.swapForDiscardPile(swapForDiscardRequest.username(), *positionRes);
  handleGameManagerResult(res, c);
}

static void knock(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RequestWrapper::KindCase::kKnockRequest>(serviceRequest, c)) {
    return;
  }

  auto &knockRequest = serviceRequest.knock_request();
  if (usernameMismatch(knockRequest.username(), c)) {
    return;
  }
  auto res = gm.knock(knockRequest.username());
  handleGameManagerResult(res, c);
}

typedef std::function<void(const GolfServiceRequest &, struct mg_connection *)> handler;
typedef std::function<absl::StatusOr<GolfServiceRequest>(std::vector<string>)> argReader;

// make this map<string, pair<handler, parser>>
const std::unordered_map<string, handler> handlers{
    {"register", registerUser},
    {"new", newGame},
    {"join", joinGame},
    {"peek", peekAtDrawPile},
    {"discardDraw", discardFromDrawPile},
    {"swapDraw", swapForDrawPile},
    {"swapDiscard", swapForDiscardPile},
    {"knock", knock},
};

static void handleMessage(struct mg_ws_message *wm, struct mg_connection *c) {
  std::scoped_lock lock(m);

  const string requestText(wm->data.ptr);
  golf_ws::RequestWrapper requestWrapper;
  auto status = google::protobuf::util::JsonStringToMessage(requestText, &requestWrapper);
  if (!status.ok()) {
    auto messageString = status.message().as_string();
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
  handler(requestWrapper, c);
}

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_HTTP_MSG) {
    auto *hm = (struct mg_http_message *)ev_data;
    if (mg_http_match_uri(hm, "/golf/ws")) {
      mg_ws_upgrade(c, hm, nullptr);
    } else if (mg_http_match_uri(hm, "/golf/stats")) {
      mg_http_reply(c, 200, "", "\"stats\": []");
    } else if (mg_http_match_uri(hm, "/golf/ui")) {
      struct mg_http_serve_opts opts = {.root_dir = nullptr};
      mg_http_serve_file(c, hm, "web/golf_ui/index.html", &opts);
    } else {
      mg_http_reply(c, 404, "", R"({"message": "not_found"})");
    }
  } else if (ev == MG_EV_WS_MSG) {
    auto *wm = (struct mg_ws_message *)ev_data;
    handleMessage(wm, c);
  } else if (ev == MG_EV_CLOSE) {
    // handle disconnect here...
  }
}

int main() {
  struct mg_mgr mgr {};
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "http://0.0.0.0:8000", fn, nullptr);
  std::cout << "listening on port 8000\n";
  for (;;) {
    mg_mgr_poll(&mgr, 500);
  }
  mg_mgr_free(&mgr);
  return 0;
}
