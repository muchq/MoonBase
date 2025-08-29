#ifndef CPP_GOLF_SERVICE_ROUTER_H
#define CPP_GOLF_SERVICE_ROUTER_H

#include "cpp/golf_service/handlers.h"
#include "mongoose.h"

namespace golf_service {
class Router {
 public:
  explicit Router(std::shared_ptr<Handler> handler) : handler_(handler) {}
  void route(struct ::mg_connection* c, int ev, void* ev_data) const;

 private:
  std::shared_ptr<Handler> handler_;
};
}  // namespace golf_service

#endif  // CPP_GOLF_SERVICE_ROUTER_H
