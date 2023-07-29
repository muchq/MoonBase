#ifndef CPP_GOLF_SERVICE_HANDLERS_H
#define CPP_GOLF_SERVICE_HANDLERS_H

#include <functional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "cpp/cards/golf/golf.h"
#include "mongoose.h"
#include "protos/golf_ws/golf_ws.pb.h"

namespace golf_service {
typedef golf_ws::RequestWrapper GolfServiceRequest;
typedef std::function<void(const GolfServiceRequest &, struct ::mg_connection *)> handler;
typedef std::function<absl::StatusOr<GolfServiceRequest>(std::vector<std::string>)> argReader;
typedef absl::StatusOr<GolfServiceRequest> StatusOrRequest;

void handleDisconnect(struct ::mg_connection *c);
void handleMessage(struct ::mg_ws_message *wm, struct ::mg_connection *c);
}  // namespace golf_service

#endif
