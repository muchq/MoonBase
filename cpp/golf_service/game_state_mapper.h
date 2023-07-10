#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include <memory>
#include <string>

#include "cpp/cards/card_mapper.h"
#include "cpp/cards/golf/golf.h"

namespace golf {

class GameStateMapper {
 public:
  static std::string gameStateToString(GameStatePtr gameStatePtr);

 private:
  CardMapper cm;
};
}  // namespace golf

#endif
