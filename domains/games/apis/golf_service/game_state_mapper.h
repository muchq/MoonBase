#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include <string>

#include "domains/games/libs/cards/card_mapper.h"
#include "domains/games/libs/cards/golf/golf.h"
#include "domains/games/protos/golf_ws/golf_ws.pb.h"

namespace golf {

class GameStateMapper {
 public:
  GameStateMapper(const CardMapper _cm) : card_mapper(_cm) {}
  golf_ws::GameStateResponse gameStateToProto(const GameStatePtr& gameStatePtr,
                                              const std::string& username) const;

 private:
  const CardMapper card_mapper;
};
}  // namespace golf

#endif
