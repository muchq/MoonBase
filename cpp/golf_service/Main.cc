#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "cpp/cards/golf/golf.h"
#include "cpp/golf_service/game_state_mapper.h"
#include "mongoose.h"

typedef struct Args {
  std::string username;
  std::string gameId;
  int players;
  golf::Position position;
  struct mg_connection *c;
} Args;

typedef struct COMMAND {
  const char *name;
  std::string (*command)(Args);
} API;

std::mutex m;
std::unordered_map<std::string, mg_connection *> connectionsByUser;
golf::GameManager gm;
golf::GameStateMapper gsm;

static void registerUser(const Args &args) {
  auto res = gm.registerUser(args.username);
  std::string output = "";
  if (!res.ok()) {
    output.append("error|");
    output.append(res.status().message());
    mg_ws_send(args.c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  output.append("ok");
  std::string user = *res;
  connectionsByUser.insert({user, args.c});
  mg_ws_send(args.c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
}

static bool usernameMismatch(const Args &args) {
  if (connectionsByUser.find(args.username) == connectionsByUser.end() ||
      connectionsByUser.at(args.username) != args.c) {
    std::string output("error|username mismatch");
    mg_ws_send(args.c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return true;
  }
  return false;
}

static void handleGameManagerResult(const absl::StatusOr<golf::GameStatePtr> &res,
                                    const Args &args) {
  if (!res.ok()) {
    std::string output = "error|";
    output.append(res.status().message());
    mg_ws_send(args.c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  auto gameStatePtr = *res;
  for (auto &user : gm.getUsersByGameId(args.gameId)) {
    auto stateForuser = gsm.gameStateJson(gameStatePtr, user);
    auto c = connectionsByUser.at(user);
    mg_ws_send(c, stateForuser.c_str(), stateForuser.size(), WEBSOCKET_OP_TEXT);
  }
}

static void newGame(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }

  auto res = gm.newGame(args.username, args.players);
  std::string output = "";
  if (!res.ok()) {
    output.append("error|");
    output.append(res.status().message());
  } else {
    output.append(gsm.gameStateJson(*res, args.username));
  }
  mg_ws_send(args.c, output.c_str(), output.size(), WEBSOCKET_OP_TEXT);
}

static void joinGame(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }
  auto res = gm.joinGame(args.gameId, args.username);
  handleGameManagerResult(res, args);
}

static void peekAtDrawPile(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }
  auto res = gm.peekAtDrawPile(args.username);
  handleGameManagerResult(res, args);
}

static void discardFromDrawPile(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }
  auto res = gm.swapDrawForDiscardPile(args.username);
  handleGameManagerResult(res, args);
}

static void swapForDrawPile(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }
  auto res = gm.swapForDrawPile(args.username, args.position);
  handleGameManagerResult(res, args);
}

static void swapForDiscardPile(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }
  auto res = gm.swapForDiscardPile(args.username, args.position);
  handleGameManagerResult(res, args);
}

static void knock(const Args &args) {
  if (usernameMismatch(args)) {
    return;
  }
  auto res = gm.knock(args.username);
  handleGameManagerResult(res, args);
}

const std::unordered_map<std::string, void (*)(const Args &)> handlers{
    {"register", registerUser},
    {"new", newGame},
    {"join", joinGame},
    {"peek", peekAtDrawPile},
    {"discardDraw", discardFromDrawPile},
    {"swapDraw", swapForDrawPile},
    {"swapDiscard", swapForDiscardPile},
    {"knock", knock},
};

static absl::StatusOr<Args> parseArgs(std::vector<std::string> &parts, struct mg_connection *c) {
  if (parts.size() != 5) {
    return absl::InvalidArgumentError("args -> <user>|<game>|<numPlayers>|<pos>");
  }
  std::string username = parts[1];
  std::string gameId = parts[2];
  int numberOfPlayers;
  try {
    numberOfPlayers = std::stoi(parts[3]);
  } catch (std::invalid_argument const &ex) {
    return absl::InvalidArgumentError(ex.what());
  } catch (std::out_of_range const &ex) {
    return absl::InvalidArgumentError(ex.what());
  }
  golf::Position position;
  if (parts[4] == "tl") {
    position = golf::Position::TopLeft;
  } else if (parts[4] == "tr") {
    position = golf::Position::TopRight;
  } else if (parts[4] == "bl") {
    position = golf::Position::BottomLeft;
  } else if (parts[4] == "br") {
    position = golf::Position::BottomRight;
  } else {
    return absl::InvalidArgumentError("invalid position. must be in (tl, tr, bl, br)");
  }
  std::cout << "username '" << username << "'\n";
  std::cout << "gameId '" << gameId << "'\n";
  std::cout << "num '" << numberOfPlayers << "'\n";

  return Args{username, gameId, numberOfPlayers, position, c};
}

static void handleMessage(struct mg_ws_message *wm, struct mg_connection *c) {
  std::scoped_lock lock(m);

  std::vector<std::string> commandParts =
      absl::StrSplit(std::string(wm->data.ptr), '|', absl::SkipWhitespace());

  if (commandParts.size() < 2) {
    std::string response = "error|arg count";
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
  }

  std::string command = commandParts[0];
  auto res = parseArgs(commandParts, c);
  if (!res.ok()) {
    std::string response = "error|";
    response.append(res.status().message());
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
    return;
  }

  Args args = *res;

  auto cmdIter = handlers.find(command);
  if (cmdIter == handlers.end()) {
    std::string response = "error|bad_command";
    mg_ws_send(c, response.c_str(), response.size(), WEBSOCKET_OP_TEXT);
  }

  cmdIter->second(args);
}

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    if (mg_http_match_uri(hm, "/golf/ws")) {
      mg_ws_upgrade(c, hm, NULL);
    } else if (mg_http_match_uri(hm, "/golf/stats")) {
      mg_http_reply(c, 200, "", "\"stats\": []");
    } else {
      mg_http_reply(c, 404, "", "{\"message\": \"not_found\"}");
    }
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
    handleMessage(wm, c);
  } else if (ev == MG_EV_CLOSE) {
    // handle disconnect here...
  }
}

int main() {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "http://0.0.0.0:8000", fn, NULL);
  for (;;) {
    mg_mgr_poll(&mgr, 500);
  }
  mg_mgr_free(&mgr);
  return 0;
}
