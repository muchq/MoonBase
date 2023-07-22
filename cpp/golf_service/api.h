#ifndef CPP_GOLF_SERVICE_API_H
#define CPP_GOLF_SERVICE_API_H

#include <functional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/golf/golf.h"
#include "protos/golf_ws/game_state_response.pb.h"

namespace golf_service {
typedef golf_ws::RequestWrapper GolfServiceRequest;
typedef std::function<void(const GolfServiceRequest&, struct mg_connection*)> handler;
typedef std::function<absl::StatusOr<GolfServiceRequest>(std::vector<std::string>)> argReader;
typedef absl::StatusOr<GolfServiceRequest> StatusOrRequest;
}  // namespace golf_service

#endif
