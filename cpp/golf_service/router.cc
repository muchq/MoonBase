#include "cpp/golf_service/router.h"

#include "cpp/golf_service/handlers.h"
#include "mongoose.h"

void golf_service::router(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_HTTP_MSG) {
    auto *hm = (struct mg_http_message *)ev_data;
    if (mg_http_match_uri(hm, "/golf/ws")) {
      mg_ws_upgrade(c, hm, nullptr);
    } else if (mg_http_match_uri(hm, "/golf/stats")) {
      mg_http_reply(c, 200, "", "\"stats\": []");
    } else if (mg_http_match_uri(hm, "/golf/ui")) {
      struct mg_http_serve_opts opts = {.root_dir = nullptr};
      mg_http_serve_file(c, hm, "web/golf_ui/index.html", &opts);
    } else {
      mg_http_reply(c, 404, "", R"({"message": "not_found"})");
    }
  } else if (ev == MG_EV_WS_MSG) {
    auto *wm = (struct mg_ws_message *)ev_data;
    golf_service::handleMessage(wm, c);
  } else if (ev == MG_EV_CLOSE) {
    golf_service::handleDisconnect(c);
  }
}
