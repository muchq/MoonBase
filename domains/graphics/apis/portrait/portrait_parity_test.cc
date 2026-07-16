// Phase 4 differential replay (https://github.com/muchq/MoonBase/issues/1168):
// the meerkat stack (the pre-cutover binary's wiring) and the smithy stack
// (the production binary's wiring) serve side by side in-process, and a
// corpus of valid and invalid requests replays against both. Every
// divergence must be on the documented intentional list:
//   - error body shape (meerkat {"error": msg} vs smithy ValidationException
//     / modeled errors) — statuses still match
//   - /health body (meerkat adds a timestamp)
//   - wrong method on a route: 404 -> 405 (smithy router knows the route)
//   - non-JSON content type: 200 -> 415 (meerkat ignored content-type)
//   - color channel > 255: 200 -> 400 (meerkat's unsigned char wrapped
//     silently; the Smithy model validates 0-255 for real)
// Successful renders must be identical JSON — same renderer, same base64.
// A final smoke logs render latency for both stacks (informational only).
// This test retires together with the meerkat path after the soak.

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

#include "domains/graphics/apis/portrait/smithy_handler.h"
#include "domains/graphics/apis/portrait/tracer_service.h"
#include "domains/graphics/apis/portrait/types.h"
#include "domains/platform/libs/meerkat/meerkat.h"
#include "moonbase/portrait/server.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/socket_transport.h"
#include "smithy/server/middleware.h"

namespace {

using json = nlohmann::json;

json BaseRequest() {
  return json::parse(R"({
    "scene": {
      "backgroundColor": [10, 20, 30],
      "backgroundStarProbability": 0.0,
      "spheres": [
        {"center": [0.0, -1.0, 3.0], "radius": 1.0, "color": [255, 0, 0],
         "specular": 500.0, "reflective": 0.2}
      ],
      "lights": [
        {"lightType": "ambient", "intensity": 0.4, "position": [0.0, 0.0, 0.0]}
      ]
    },
    "perspective": {"cameraPosition": [0.0, 0.0, -1.0], "cameraFocus": [0.0, 0.0, 0.0]},
    "output": {"width": 20, "height": 20}
  })");
}

// Both stacks wired as their binaries wire them, minus OTel and the rate
// limiter (neither shapes route semantics; 429/413 behavior is pinned by
// smithy_middleware_test). Started once per suite — meerkat's stop() waits
// out a 100ms poll, so per-case restarts would spend ~1s doing nothing.
// Sharing the TracerService caches across cases is safe: they are
// content-keyed, and every assertion compares old vs new within one case.
class PortraitParityTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    old_service_ = std::make_unique<portrait::TracerService>();
    old_server_ = std::make_unique<meerkat::HttpServer>();
    old_server_->post(
        "/portrait/v1/trace",
        meerkat::wrap<portrait::TraceRequest, portrait::TraceResponse>(
            [](portrait::TraceRequest& request) { return old_service_->trace(request); }));
    old_server_->enable_health_checks();
    ASSERT_TRUE(old_server_->listen("127.0.0.1", 0));
    old_thread_ = std::async(std::launch::async, [] { old_server_->run(); });
    for (int i = 0; i < 100 && !old_server_->is_listening(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(old_server_->is_listening());

    new_server_ = std::make_unique<moonbase::portrait::PortraitServer>(
        std::make_shared<portrait::SmithyTracerHandler>());
    transport_ = std::make_unique<smithy::http::BeastServerTransport>(
        smithy::http::BeastServerTransport::Options{});
    ASSERT_TRUE(transport_
                    ->Start(smithy::server::Chain({smithy::server::HealthEndpoint("/health")},
                                                  new_server_->Handler()))
                    .ok());
  }

  static void TearDownTestSuite() {
    transport_->Stop();
    old_server_->stop();
    if (old_thread_.valid()) old_thread_.wait();
    transport_.reset();
    new_server_.reset();
    old_server_.reset();
    old_service_.reset();
  }

  static smithy::http::HttpResponse Send(int port, const std::string& method,
                                         const std::string& target, const std::string& body,
                                         const char* content_type) {
    smithy::http::SocketHttpClient client("127.0.0.1", port);
    smithy::http::HttpRequest request;
    request.method = method;
    request.target = target;
    request.body = body;
    if (content_type != nullptr) {
      request.headers.Set("content-type", content_type);
    }
    auto response = client.Send(request);
    if (!response.ok()) {
      ADD_FAILURE() << "send to port " << port << " failed: " << response.error().message();
      return {};
    }
    return *response;
  }

  static std::unique_ptr<portrait::TracerService> old_service_;
  static std::unique_ptr<meerkat::HttpServer> old_server_;
  static std::future<void> old_thread_;
  static std::unique_ptr<moonbase::portrait::PortraitServer> new_server_;
  static std::unique_ptr<smithy::http::BeastServerTransport> transport_;
};

std::unique_ptr<portrait::TracerService> PortraitParityTest::old_service_;
std::unique_ptr<meerkat::HttpServer> PortraitParityTest::old_server_;
std::future<void> PortraitParityTest::old_thread_;
std::unique_ptr<moonbase::portrait::PortraitServer> PortraitParityTest::new_server_;
std::unique_ptr<smithy::http::BeastServerTransport> PortraitParityTest::transport_;

struct ParityCase {
  const char* name;
  const char* method;
  const char* target;
  const char* content_type;  // nullptr sends no content-type header
  std::string (*body)();     // nullptr sends no body
  int old_status;
  int new_status;
  bool identical_json;  // parse both bodies as JSON and require equality
};

class PortraitParityCaseTest : public PortraitParityTest,
                               public ::testing::WithParamInterface<ParityCase> {};

TEST_P(PortraitParityCaseTest, OldAndNewAgree) {
  const ParityCase& c = GetParam();
  const std::string body = c.body != nullptr ? c.body() : "";

  const auto old_response = Send(old_server_->get_port(), c.method, c.target, body, c.content_type);
  const auto new_response = Send(transport_->port(), c.method, c.target, body, c.content_type);

  EXPECT_EQ(old_response.status, c.old_status) << old_response.body;
  EXPECT_EQ(new_response.status, c.new_status) << new_response.body;
  if (c.identical_json) {
    EXPECT_EQ(json::parse(old_response.body), json::parse(new_response.body));
  }
}

std::string ValidBody() { return BaseRequest().dump(); }

std::string OmittedOptionalsBody() {
  json r = BaseRequest();
  r["scene"].erase("backgroundColor");
  r["scene"].erase("backgroundStarProbability");
  r["scene"].erase("lights");
  return r.dump();
}

std::string ElevenSpheresBody() {
  json r = BaseRequest();
  const json sphere = r["scene"]["spheres"][0];
  for (int i = 0; i < 11; ++i) r["scene"]["spheres"][i] = sphere;
  return r.dump();
}

constexpr char kTrace[] = "/portrait/v1/trace";
constexpr char kJson[] = "application/json";

INSTANTIATE_TEST_SUITE_P(
    Corpus, PortraitParityCaseTest,
    ::testing::Values(
        // Identical successes: same renderer, same base64 encoder.
        ParityCase{"ValidScene", "POST", kTrace, kJson, ValidBody, 200, 200, true},
        ParityCase{"OmittedOptionals", "POST", kTrace, kJson, OmittedOptionalsBody, 200, 200, true},
        // Rejections agree on status; body shapes differ by design.
        ParityCase{"CameraAtFocus", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["perspective"]["cameraFocus"] = r["perspective"]["cameraPosition"];
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"ZeroRadius", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["scene"]["spheres"][0]["radius"] = 0.0;
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"ExtremeAspectRatio", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["output"] = {{"width", 1200}, {"height", 20}};
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"ElevenSpheres", "POST", kTrace, kJson, ElevenSpheresBody, 400, 400, false},
        ParityCase{"RadiusTooLarge", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["scene"]["spheres"][0]["radius"] = 20000.0;
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"MissingPerspective", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r.erase("perspective");
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"UnknownLightType", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["scene"]["lights"][0]["lightType"] = "spot";
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"MalformedJson", "POST", kTrace, kJson, [] { return std::string("{"); }, 400,
                   400, false},
        ParityCase{"TwoElementVector", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["scene"]["spheres"][0]["center"] = {1.0, 2.0};
                     return r.dump();
                   },
                   400, 400, false},
        ParityCase{"UnknownPath", "POST", "/nope", kJson, ValidBody, 404, 404, false},
        ParityCase{"Health", "GET", "/health", nullptr, nullptr, 200, 200, false},
        // Documented intentional divergences.
        ParityCase{"ColorChannelAbove255", "POST", kTrace, kJson,
                   [] {
                     json r = BaseRequest();
                     r["scene"]["spheres"][0]["color"][0] = 300;
                     return r.dump();
                   },
                   200, 400, false},
        ParityCase{"GetOnTraceRoute", "GET", kTrace, nullptr, nullptr, 404, 405, false},
        ParityCase{"NonJsonContentType", "POST", kTrace, "text/plain", ValidBody, 200, 415, false}),
    [](const auto& info) { return std::string(info.param.name); });

// Perf smoke, informational only: three distinct fresh renders per stack so
// neither hits its response cache; timings land in the test log.
TEST_F(PortraitParityTest, RenderLatencySmoke) {
  for (const auto& [label, port] : {std::pair<const char*, int>{"meerkat", old_server_->get_port()},
                                    {"smithy", transport_->port()}}) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
      json r = BaseRequest();
      r["scene"]["spheres"][0]["center"][0] = 0.3 * (i + 1);
      const auto response = Send(port, "POST", kTrace, r.dump(), kJson);
      ASSERT_EQ(response.status, 200);
    }
    const auto total = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    std::cout << "[ perf ] " << label << ": 3 fresh 20x20 renders in " << total.count() << "us\n";
  }
}

}  // namespace
