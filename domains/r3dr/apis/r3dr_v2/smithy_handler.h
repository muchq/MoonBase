#ifndef DOMAINS_R3DR_APIS_R3DR_V2_SMITHY_HANDLER_H
#define DOMAINS_R3DR_APIS_R3DR_V2_SMITHY_HANDLER_H

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "domains/r3dr/apis/r3dr_v2/shortener.h"
#include "moonbase/r3dr/server.h"
#include "smithy/core/error.h"
#include "smithy/core/outcome.h"
#include "smithy/server/router.h"

namespace r3dr_v2 {

/// kInvalidArgument -> InvalidRequestError (400); everything else (the
/// store) -> unmodeled 500 with a fixed body.
smithy::Error ToSmithyError(const absl::Status& status);

/// Type-mapping shim over Shortener, whose pieces (aura::Cache, pg::Client)
/// are themselves thread-safe — the whole story for the transport's pool.
class SmithyShortenerHandler final : public moonbase::r3dr::R3drV2Handler {
 public:
  explicit SmithyShortenerHandler(std::shared_ptr<Shortener> shortener)
      : shortener_(std::move(shortener)) {}

  smithy::Outcome<moonbase::r3dr::ShortenOutput> Shorten(
      const moonbase::r3dr::ShortenInput& input,
      const smithy::server::RequestContext& context) override;

  /// A miss is the modeled 404; a store failure is a 500, not a 404.
  smithy::Outcome<moonbase::r3dr::RedirectOutput> Redirect(
      const moonbase::r3dr::RedirectInput& input,
      const smithy::server::RequestContext& context) override;

 private:
  std::shared_ptr<Shortener> shortener_;
};

}  // namespace r3dr_v2

#endif  // DOMAINS_R3DR_APIS_R3DR_V2_SMITHY_HANDLER_H
