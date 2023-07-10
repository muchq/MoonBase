#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include <string>

#include "cpp/cards/card_mapper.h"
#include "cpp/cards/golf/golf.h"

namespace golf {

class GameStateMapper {
 public:
  static std::string gameStateJson(GameStatePtr gameStatePtr, const std::string& username);

 private:
  static CardMapper cm;
  static std::string writeString(const std::string& name, const std::string& value);
  static std::string writeInt(const std::string& name, const int value);
  static std::string writeBool(const std::string& name, const bool value);
};
}  // namespace golf

#endif
