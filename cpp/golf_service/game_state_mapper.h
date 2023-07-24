#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include <string>

#include "cpp/cards/card_mapper.h"
#include "cpp/cards/golf/golf.h"
#include "protos/golf_ws/golf_ws.pb.h"

namespace golf {

class GameStateMapper {
 public:
  GameStateMapper(const CardMapper _cm) : cm(_cm) {}
  golf_ws::GameStateResponse gameStateToProto(const GameStatePtr& gameStatePtr,
                                              const std::string& username) const;

 private:
  const CardMapper cm;
};
}  // namespace golf

#endif
