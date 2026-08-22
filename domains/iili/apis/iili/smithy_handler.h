#ifndef DOMAINS_IILI_APIS_IILI_SMITHY_HANDLER_H
#define DOMAINS_IILI_APIS_IILI_SMITHY_HANDLER_H

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "domains/iili/apis/iili/shortener.h"
#include "moonbase/iili/server.h"
#include "smithy/core/error.h"
#include "smithy/core/outcome.h"
#include "smithy/http/transport.h"
#include "smithy/server/router.h"

namespace iili {

/// kInvalidArgument -> InvalidRequestError (400); everything else (the
/// store) -> unmodeled 500 with a fixed body.
smithy::Error ToSmithyError(const absl::Status& status);

/// Type-mapping shim over Shortener, whose pieces (aura::Cache, pg::Client)
/// are themselves thread-safe — the whole story for the transport's pool.
class SmithyShortenerHandler final : public moonbase::iili::IiliHandler {
 public:
  explicit SmithyShortenerHandler(std::shared_ptr<Shortener> shortener)
      : shortener_(std::move(shortener)) {}

  smithy::Outcome<moonbase::iili::ShortenOutput> Shorten(
      const moonbase::iili::ShortenInput& input,
      const smithy::server::RequestContext& context) override;

  /// A miss is the modeled 404; a store failure is a 500, not a 404.
  smithy::Outcome<moonbase::iili::RedirectOutput> Redirect(
      const moonbase::iili::RedirectInput& input,
      const smithy::server::RequestContext& context) override;

 private:
  std::shared_ptr<Shortener> shortener_;
};

/// Answers HEAD from the GET routes (#1433). The router buckets by exact
/// method, so an unmodeled HEAD 405s; unfurlers lead with it. The body is
/// dropped on the way out — content on a HEAD response desynchronizes a
/// keep-alive connection.
smithy::http::RequestHandler WithHeadAsGet(smithy::http::RequestHandler next);

}  // namespace iili

#endif  // DOMAINS_IILI_APIS_IILI_SMITHY_HANDLER_H
