#include "cpp/golf_service/api.h"

#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace golf_service {
using std::string;
using std::vector;

StatusOrRequest readRegisterUserRequest(const vector<string> &args) {
  if (args.size() != 2) {
    return absl::InvalidArgumentError("registerUser|<username>|");
  }
  return RegisterUserRequest{args[1]};
}

StatusOrRequest readNewGameRequest(const std::vector<string> &parts) {
  if (parts.size() != 3) {
    return absl::InvalidArgumentError("new|<user>|<numPlayers>|");
  }
  int numberOfPlayers;
  try {
    numberOfPlayers = std::stoi(parts[2]);
  } catch (std::invalid_argument const &ex) {
    return absl::InvalidArgumentError(ex.what());
  } catch (std::out_of_range const &ex) {
    return absl::InvalidArgumentError(ex.what());
  }

  return NewGameRequest{parts[1], numberOfPlayers};
}

StatusOrRequest readJoinGameRequest(const std::vector<string> &parts) {
  if (parts.size() != 3) {
    return absl::InvalidArgumentError("join|<user>|<gameId>|");
  }
  return JoinGameRequest{parts[1], parts[2]};
}

StatusOrRequest readPeekRequest(const std::vector<string> &parts) {
  if (parts.size() != 2) {
    return absl::InvalidArgumentError("peek|<user>|");
  }
  return PeekRequest{parts[1]};
}

StatusOrRequest readDiscardDrawRequest(const std::vector<string> &parts) {
  if (parts.size() != 2) {
    return absl::InvalidArgumentError("discardDraw|<user>|");
  }
  return DiscardDrawRequest{parts[1]};
}

StatusOrRequest readSwapForDrawRequest(const std::vector<string> &parts) {
  if (parts.size() != 3) {
    return absl::InvalidArgumentError("swapDraw|<user>|<position>|");
  }
  golf::Position position;
  if (parts[2] == "tl") {
    position = golf::Position::TopLeft;
  } else if (parts[2] == "tr") {
    position = golf::Position::TopRight;
  } else if (parts[2] == "bl") {
    position = golf::Position::BottomLeft;
  } else if (parts[2] == "br") {
    position = golf::Position::BottomRight;
  } else {
    return absl::InvalidArgumentError("invalid position. must be in (tl, tr, bl, br)");
  }

  return SwapForDrawRequest{parts[1], position};
}

StatusOrRequest readSwapForDiscardRequest(const std::vector<string> &parts) {
  if (parts.size() != 3) {
    return absl::InvalidArgumentError("swapDiscard|<user>|<position>|");
  }
  golf::Position position;
  if (parts[2] == "tl") {
    position = golf::Position::TopLeft;
  } else if (parts[2] == "tr") {
    position = golf::Position::TopRight;
  } else if (parts[2] == "bl") {
    position = golf::Position::BottomLeft;
  } else if (parts[2] == "br") {
    position = golf::Position::BottomRight;
  } else {
    return absl::InvalidArgumentError("invalid position. must be in (tl, tr, bl, br)");
  }

  return SwapForDiscardRequest{parts[1], position};
}

StatusOrRequest readKnockRequest(const std::vector<string> &parts) {
  if (parts.size() != 2) {
    return absl::InvalidArgumentError("knock|<user>|");
  }
  return KnockRequest{parts[1]};
}

}  // namespace golf_service