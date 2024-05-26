#ifndef CPP_GOLF_SERVICE_ROUTER_H
#define CPP_GOLF_SERVICE_ROUTER_H

#include "mongoose.h"

namespace golf_service {
void router(struct ::mg_connection *c, int ev, void *ev_data);
}  // namespace golf_service

#endif  // CPP_GOLF_SERVICE_ROUTER_H
