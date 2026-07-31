#ifndef CPP_PORTRAIT_SMITHY_HANDLER_H
#define CPP_PORTRAIT_SMITHY_HANDLER_H

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "domains/graphics/apis/portrait/tracer_service.h"
#include "moonbase/portrait/server.h"
#include "smithy/core/error.h"
#include "smithy/core/outcome.h"
#include "smithy/server/router.h"

namespace portrait {

/// Maps a TracerService failure onto the operation's declared error space:
/// kInvalidArgument -> InvalidSceneError (400), kResourceExhausted ->
/// RenderCapacityError (503), everything else -> unmodeled, which the
/// generated server answers as a 500 whose body is fixed at
/// {"__type":"InternalFailure","message":"internal failure"}.
///
/// Exhaustive by test rather than by compiler. absl::StatusCode is an open
/// enum, so a switch here needs a default whatever we do; what stops a newly
/// returned status from becoming a silent 500 is the table in
/// smithy_handler_test.cc, which pins the outcome for every enumerator.
/// Adding a status to TracerService therefore means editing a visible row.
///
/// Free function rather than a private method so that table can exercise
/// codes the renderer cannot currently produce.
smithy::Error ToSmithyError(const absl::Status& status);

/// Serves the generated Smithy Portrait API by wrapping TracerService:
/// generated inputs convert to the portrait domain types, so TracerService
/// keeps its validation, response cache, and metrics unchanged. Cross-field
/// rules the constraint traits can't express (camera != focus, aspect ratio,
/// strictly positive radius) surface as the modeled InvalidSceneError.
///
/// Must be thread-safe: transports dispatch one handler instance across a
/// thread pool. TracerService's cache and metrics are mutex-guarded/atomic,
/// and each render constructs its own tracy::Tracer.
class SmithyTracerHandler final : public moonbase::portrait::PortraitHandler {
 public:
  SmithyTracerHandler() : tracer_service_(std::make_unique<TracerService>()) {}
  explicit SmithyTracerHandler(uint16_t cache_size)
      : tracer_service_(std::make_unique<TracerService>(cache_size)) {}
  /// Takes ownership of the service. The seam a test uses to install a
  /// renderer that fails, so the failure paths can be driven through the real
  /// handler and the generated server rather than asserted a layer at a time.
  explicit SmithyTracerHandler(std::unique_ptr<TracerService> tracer_service)
      : tracer_service_(std::move(tracer_service)) {}

  smithy::Outcome<moonbase::portrait::TraceOutput> Trace(
      const moonbase::portrait::TraceInput& input,
      const smithy::server::RequestContext& context) override;

 private:
  std::unique_ptr<TracerService> tracer_service_;
};

}  // namespace portrait

#endif
