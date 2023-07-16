#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include <string>

#include "cpp/cards/card_mapper.h"
#include "cpp/cards/golf/golf.h"

namespace golf {

using std::string;

class GameStateMapper {
 public:
  static string gameStateJson(const GameStatePtr& gameStatePtr, const string& username);

 private:
  static CardMapper cm;
  static string writeString(const string& name, const string& value);
  static string writeInt(const string& name, const int value);
  static string writeBool(const string& name, const bool value);
};
}  // namespace golf

#endif
