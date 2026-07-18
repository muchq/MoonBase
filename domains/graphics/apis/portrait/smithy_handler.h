#ifndef CPP_PORTRAIT_SMITHY_HANDLER_H
#define CPP_PORTRAIT_SMITHY_HANDLER_H

#include <cstdint>

#include "domains/graphics/apis/portrait/tracer_service.h"
#include "moonbase/portrait/server.h"
#include "smithy/core/outcome.h"
#include "smithy/server/router.h"

namespace portrait {

/// Serves the generated Smithy Portrait API by wrapping TracerService
/// (phase 2 of https://github.com/muchq/MoonBase/issues/1168): generated
/// inputs convert to the legacy portrait types, so TracerService keeps its
/// validation, response cache, and metrics unchanged. Cross-field rules the
/// constraint traits can't express (camera != focus, aspect ratio, strictly
/// positive radius) surface as the modeled InvalidSceneError.
///
/// Must be thread-safe: transports dispatch one handler instance across a
/// thread pool. TracerService's cache and metrics are mutex-guarded/atomic,
/// and each render constructs its own tracy::Tracer.
class SmithyTracerHandler final : public moonbase::portrait::PortraitHandler {
 public:
  SmithyTracerHandler() = default;
  explicit SmithyTracerHandler(uint16_t cache_size) : tracer_service_(cache_size) {}

  smithy::Outcome<moonbase::portrait::TraceOutput> Trace(
      const moonbase::portrait::TraceInput& input,
      const smithy::server::RequestContext& context) override;

 private:
  TracerService tracer_service_;
};

}  // namespace portrait

#endif
