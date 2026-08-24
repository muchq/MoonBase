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

  /// The HEAD form, answering exactly what Redirect does. The transport drops
  /// the body and keeps the length, so this side has no framing to do.
  smithy::Outcome<moonbase::iili::RedirectHeadOutput> RedirectHead(
      const moonbase::iili::RedirectHeadInput& input,
      const smithy::server::RequestContext& context) override;

 private:
  /// Slug to Location, or the modeled 404 — the one resolution both redirect
  /// operations answer from.
  smithy::Outcome<moonbase::iili::RedirectOutput> Resolve(const std::string& slug);

  std::shared_ptr<Shortener> shortener_;
};

}  // namespace iili

#endif  // DOMAINS_IILI_APIS_IILI_SMITHY_HANDLER_H
