#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "cpp/cards/golf/golf.h"
#include "cpp/golf_service/api.h"
#include "cpp/golf_service/game_state_mapper.h"
#include "mongoose.h"

using golf::GameStateMapper;
using golf_service::DiscardDrawRequest;
using golf_service::GolfServiceRequest;
using golf_service::JoinGameRequest;
using golf_service::KnockRequest;
using golf_service::NewGameRequest;
using golf_service::PeekRequest;
using golf_service::readDiscardDrawRequest;
using golf_service::readJoinGameRequest;
using golf_service::readKnockRequest;
using golf_service::readNewGameRequest;
using golf_service::readPeekRequest;
using golf_service::readRegisterUserRequest;
using golf_service::readSwapForDiscardRequest;
using golf_service::readSwapForDrawRequest;
using golf_service::RegisterUserRequest;
using golf_service::SwapForDiscardRequest;
using golf_service::SwapForDrawRequest;
using std::string;

std::mutex m;
std::unordered_map<std::string, mg_connection *> connectionsByUser;
golf::GameManager gm;
GameStateMapper gsm;

template <typename T>
static bool validRequestType(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (std::holds_alternative<T>(serviceRequest)) {
    return true;
  }

  string output("error|invalid request");
  mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
  return false;
}

static void registerUser(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<RegisterUserRequest>(serviceRequest, c)) {
    return;
  }

  const RegisterUserRequest registerUserRequest = std::get<RegisterUserRequest>(serviceRequest);
  // don't allow re-registration yet
  for (auto i = connectionsByUser.begin(); i != connectionsByUser.end(); i++) {
    if (connectionsByUser.at(i->first) == c) {
      string output("error|already registered");
      mg_ws_send(c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
      return;
    }
  }

  auto res = gm.registerUser(registerUserRequest.username);
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
    auto stateForUser = GameStateMapper::gameStateJson(gameStatePtr, user);
    auto userConnection = connectionsByUser.at(user);
    mg_ws_send(userConnection, stateForUser.c_str(), stateForUser.size(), WEBSOCKET_OP_TEXT);
  }
}

static void newGame(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<NewGameRequest>(serviceRequest, c)) {
    return;
  }

  auto newGameRequest = std::get<NewGameRequest>(serviceRequest);
  if (usernameMismatch(newGameRequest.username, c)) {
    return;
  }

  auto res = gm.newGame(newGameRequest.username, newGameRequest.players);
  handleGameManagerResult(res, c);
}

static void joinGame(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<JoinGameRequest>(serviceRequest, c)) {
    return;
  }

  auto joinGameRequest = std::get<JoinGameRequest>(serviceRequest);
  if (usernameMismatch(joinGameRequest.username, c)) {
    return;
  }
  auto res = gm.joinGame(joinGameRequest.gameId, joinGameRequest.username);
  handleGameManagerResult(res, c);
}

static void peekAtDrawPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<PeekRequest>(serviceRequest, c)) {
    return;
  }

  auto peekRequest = std::get<PeekRequest>(serviceRequest);
  if (usernameMismatch(peekRequest.username, c)) {
    return;
  }
  auto res = gm.peekAtDrawPile(peekRequest.username);
  handleGameManagerResult(res, c);
}

static void discardFromDrawPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<DiscardDrawRequest>(serviceRequest, c)) {
    return;
  }

  auto discardDrawRequest = std::get<DiscardDrawRequest>(serviceRequest);
  if (usernameMismatch(discardDrawRequest.username, c)) {
    return;
  }
  auto res = gm.swapDrawForDiscardPile(discardDrawRequest.username);
  handleGameManagerResult(res, c);
}

static void swapForDrawPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<SwapForDrawRequest>(serviceRequest, c)) {
    return;
  }

  auto swapForDrawRequest = std::get<SwapForDrawRequest>(serviceRequest);
  if (usernameMismatch(swapForDrawRequest.username, c)) {
    return;
  }
  auto res = gm.swapForDrawPile(swapForDrawRequest.username, swapForDrawRequest.position);
  handleGameManagerResult(res, c);
}

static void swapForDiscardPile(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<SwapForDiscardRequest>(serviceRequest, c)) {
    return;
  }

  auto swapForDiscardRequest = std::get<SwapForDiscardRequest>(serviceRequest);
  if (usernameMismatch(swapForDiscardRequest.username, c)) {
    return;
  }
  auto res = gm.swapForDiscardPile(swapForDiscardRequest.username, swapForDiscardRequest.position);
  handleGameManagerResult(res, c);
}

static void knock(const GolfServiceRequest &serviceRequest, struct mg_connection *c) {
  if (!validRequestType<KnockRequest>(serviceRequest, c)) {
    return;
  }

  auto knockRequest = std::get<KnockRequest>(serviceRequest);
  if (usernameMismatch(knockRequest.username, c)) {
    return;
  }
  auto res = gm.knock(knockRequest.username);
  handleGameManagerResult(res, c);
}

typedef std::function<void(const GolfServiceRequest &, struct mg_connection *)> handler;
typedef std::function<absl::StatusOr<GolfServiceRequest>(std::vector<string>)> argReader;

// make this map<string, pair<handler, parser>>
const std::unordered_map<string, std::pair<handler, argReader>> handlers{
    {"register", {registerUser, readRegisterUserRequest}},
    {"new", {newGame, readNewGameRequest}},
    {"join", {joinGame, readJoinGameRequest}},
    {"peek", {peekAtDrawPile, readPeekRequest}},
    {"discardDraw", {discardFromDrawPile, readDiscardDrawRequest}},
    {"swapDraw", {swapForDrawPile, readSwapForDrawRequest}},
    {"swapDiscard", {swapForDiscardPile, readSwapForDiscardRequest}},
    {"knock", {knock, readKnockRequest}},
};

//static absl::StatusOr<GolfServiceRequest> parseArgs(std::vector<string> &parts,
//                                                    struct mg_connection *c) {
//  if (parts.size() != 5) {
//    return absl::InvalidArgumentError("args -> <user>|<game>|<numPlayers>|<pos>");
//  }
//  string username = parts[1];
//  string gameId = parts[2];
//  int numberOfPlayers;
//  try {
//    numberOfPlayers = std::stoi(parts[3]);
//  } catch (std::invalid_argument const &ex) {
//    return absl::InvalidArgumentError(ex.what());
//  } catch (std::out_of_range const &ex) {
//    return absl::InvalidArgumentError(ex.what());
//  }
//  golf::Position position;
//  if (parts[4] == "tl") {
//    position = golf::Position::TopLeft;
//  } else if (parts[4] == "tr") {
//    position = golf::Position::TopRight;
//  } else if (parts[4] == "bl") {
//    position = golf::Position::BottomLeft;
//  } else if (parts[4] == "br") {
//    position = golf::Position::BottomRight;
//  } else {
//    return absl::InvalidArgumentError("invalid position. must be in (tl, tr, bl, br)");
//  }
//
//  return Args{username, gameId, numberOfPlayers, position, c};
//}

static void handleMessage(struct mg_ws_message *wm, struct mg_connection *c) {
  std::scoped_lock lock(m);

  std::vector<string> commandParts =
      absl::StrSplit(string(wm->data.ptr), '|', absl::SkipWhitespace());
  if (commandParts.empty()) {
    string response = "error|arg count";
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
  }

  auto command = handlers.find(commandParts[0]);
  if (command == handlers.end()) {
    std::string response = "error|bad_command";
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  auto res = command->second.second(commandParts);
  if (!res.ok()) {
    std::string response = "error|";
    response.append(res.status().message());
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  GolfServiceRequest req = *res;

  command->second.first(req, c);
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
