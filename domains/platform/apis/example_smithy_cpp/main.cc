// Phase 0 spike server (https://github.com/muchq/MoonBase/issues/1168).
// Serving lifecycle mirrors smithy-cpp's production guide: block shutdown
// signals before the transport spawns its thread pool, sigwait, then Stop()
// drains in-flight requests.
//
//   bazel run //domains/platform/apis/example_smithy_cpp
//   curl localhost:8080/greeter/v1/greet -H 'content-type: application/json' -d '{"name":"moon"}'
//   curl localhost:8080/health

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "moonbase/greeter/server.h"
#include "moonbase/greeter/types.h"
#include "smithy/core/outcome.h"
#include "smithy/http/beast_transport.h"
#include "smithy/server/middleware.h"

namespace {

using moonbase::greeter::GreeterHandler;
using moonbase::greeter::GreeterServer;
using moonbase::greeter::GreetInput;
using moonbase::greeter::GreetOutput;
using moonbase::greeter::UnwelcomeGuest;

// Handlers must be thread-safe: the Beast transport dispatches on a thread
// pool (unlike meerkat's single-threaded event loop).
class GreetingHandler final : public GreeterHandler {
 public:
  smithy::Outcome<GreetOutput> Greet(const GreetInput& input) override {
    if (input.name == "grinch") {
      const std::string message = "not welcome here: " + input.name;
      smithy::Error error = smithy::Error::Modeled("UnwelcomeGuest", message);
      error.set_detail(UnwelcomeGuest{.message = message});
      return error;
    }
    // enthusiasm is std::optional: members of @input structures stay
    // client-optional; the server fills the @default while parsing.
    return GreetOutput{.message =
                           "hello, " + input.name +
                           std::string(static_cast<size_t>(input.enthusiasm.value_or(1)), '!')};
  }
};

int read_port(int default_port) {
  const char* env = std::getenv("PORT");
  return env != nullptr ? std::atoi(env) : default_port;
}

}  // namespace

int main() {
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

  GreeterServer server(std::make_shared<GreetingHandler>());
  // Middleware composes outside the generated router; /health here matches
  // what meerkat's enable_health_checks() provides (minus the timestamp).
  auto handler =
      smithy::server::Chain({smithy::server::HealthEndpoint("/health")}, server.Handler());

  smithy::http::BeastServerTransport transport({
      .address = "0.0.0.0",
      .port = read_port(8080),
      .drain_timeout_seconds = 10,
  });
  smithy::Outcome<smithy::Unit> started = transport.Start(handler);
  if (!started.ok()) {
    std::fprintf(stderr, "example_smithy_cpp: start failed: %s\n",
                 started.error().message().c_str());
    return 1;
  }
  std::fprintf(stderr, "example_smithy_cpp: serving on :%d\n", transport.port());

  int signal_number = 0;
  sigwait(&shutdown_signals, &signal_number);
  std::fprintf(stderr, "example_smithy_cpp: signal %d, draining\n", signal_number);
  transport.Stop();
  return 0;
}
