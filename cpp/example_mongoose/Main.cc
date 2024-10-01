#include "mongoose.h"

static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *)ev_data;
    if (mg_match(hm->uri, mg_str("/api/hello"), nullptr)) {
      mg_http_reply(c, 200, "", "{%m:%d}\n", MG_ESC("status"), 1);
    } else {
      mg_http_reply(c, 404, "", "{\"message\": \"not_found\"}");
    }
  }
}

int main() {
  struct mg_mgr mgr{};
  mg_mgr_init(&mgr);
  mg_http_listen(&mgr, "http://0.0.0.0:8000", fn, nullptr);
  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }

  return 0;
}
