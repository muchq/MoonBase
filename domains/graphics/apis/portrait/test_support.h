#ifndef CPP_PORTRAIT_TEST_SUPPORT_H
#define CPP_PORTRAIT_TEST_SUPPORT_H

// Shared fixtures and harness for the portrait test suite: the canonical
// valid scene in both its typed and JSON wire forms, and a loopback harness
// wrapping the generated server so each test file doesn't rebuild the
// server/transport/client plumbing.

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/http/loopback.h"
#include "smithy/http/transport.h"

namespace portrait::test_support {

/// The canonical minimal valid scene: one sphere, one ambient light,
/// deterministic (no background stars) and cheap to render (the 20x20
/// minimum output size). `sphere_x` varies the scene so tests can force
/// cache misses.
moonbase::portrait::TraceInput ValidTraceInput(double sphere_x = 0.0);

/// The same scene as the JSON wire body ValidTraceInput(0.0) serializes to.
std::string ValidTraceJson();

/// A generated Portrait server behind the loopback transport, optionally
/// with a middleware chain composed around the server's handler via `wrap`.
class LoopbackHarness {
 public:
  using Wrap = std::function<smithy::http::RequestHandler(smithy::http::RequestHandler)>;

  explicit LoopbackHarness(std::shared_ptr<moonbase::portrait::PortraitHandler> handler,
                           Wrap wrap = nullptr);

  /// A generated client speaking through the loopback.
  moonbase::portrait::PortraitClient MakeClient();

  /// Drives a raw request through the composed handler chain. Reports a
  /// gtest failure and returns a default response if the transport errors.
  smithy::http::HttpResponse Send(smithy::http::HttpRequest request);

  /// POSTs a body to the trace route.
  smithy::http::HttpResponse PostTrace(const std::string& body,
                                       const std::string& content_type = "application/json");

  /// The composed handler chain, for driving other transports (e.g. Beast).
  const smithy::http::RequestHandler& handler() const { return handler_; }

 private:
  std::unique_ptr<moonbase::portrait::PortraitServer> server_;
  smithy::http::RequestHandler handler_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
};

}  // namespace portrait::test_support

#endif
