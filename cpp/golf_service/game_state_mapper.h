#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include <memory>
#include <string>

#include "cpp/cards/card_mapper.h"
#include "cpp/cards/golf/golf.h"
#include "protos/golf_ws/golf_ws.pb.h"

namespace golf {

typedef std::unique_ptr<golf_ws::GameStateResponse> ResponsePtr;

class GameStateMapper {
 public:
  GameStateMapper(const CardMapper _cm) : cm(_cm) {}
  std::string gameStateJson(const GameStatePtr& gameStatePtr, const std::string& username) const;
  golf_ws::GameStateResponse gameStateToProto(const GameStatePtr& gameStatePtr,
                                              const std::string& username) const;

 private:
  const CardMapper cm;
  std::string writeString(const std::string& name, const std::string& value);
  std::string writeInt(const std::string& name, const int value);
  std::string writeBool(const std::string& name, const bool value);
};
}  // namespace golf

#endif
