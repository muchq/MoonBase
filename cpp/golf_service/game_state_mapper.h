#ifndef CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H
#define CPP_GOLF_SERVICE_GAME_STATE_MAPPER_H

#include "cpp/cards/card_mapper.h"
#include "cpp/cards/golf/game_state.h"

#include <memory>
#include <string>

namespace golf {

class GameStateMapper {
    public:
        static std::string game_state_to_string(const GameState& state);
};
}

#endif
