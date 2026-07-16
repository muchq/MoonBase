// Phase 2 handler tests (https://github.com/muchq/MoonBase/issues/1168): the
// generated client drives SmithyTracerHandler — real ray tracing — over
// loopback. Covers the happy path (a decodable PNG comes back), cache reuse,
// each cross-field rule surfacing as the modeled InvalidSceneError, and a
// concurrency hammer that regression-tests the tracy::Tracer RNG fix (run it
// under TSan to prove the data race is gone:
//   bazel test --copt=-fsanitize=thread --linkopt=-fsanitize=thread \
//     //domains/graphics/apis/portrait:smithy_handler_test).

#include "domains/graphics/apis/portrait/smithy_handler.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/client/config.h"
#include "smithy/http/loopback.h"

namespace {

using moonbase::portrait::InvalidSceneError;
using moonbase::portrait::Light;
using moonbase::portrait::LightType;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::PortraitServer;
using moonbase::portrait::Sphere;
using moonbase::portrait::TraceInput;
using portrait::SmithyTracerHandler;

constexpr uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

// A minimal valid scene: deterministic (no background stars) and cheap to
// render (the 20x20 minimum output size).
TraceInput ValidInput(double sphere_x = 0.0) {
  Sphere sphere;
  sphere.center = {sphere_x, -1.0, 3.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 500.0;
  sphere.reflective = 0.2;

  Light light;
  light.lightType = LightType::Value::kAmbient;
  light.intensity = 0.4;
  light.position = {0.0, 0.0, 0.0};

  TraceInput input;
  input.scene.spheres = {sphere};
  input.scene.lights = {light};
  input.perspective.cameraPosition = {0.0, 0.0, -1.0};
  input.perspective.cameraFocus = {0.0, 0.0, 0.0};
  input.output.width = 20;
  input.output.height = 20;
  return input;
}

bool LooksLikePng(const smithy::Blob& blob) {
  if (blob.size() < sizeof(kPngSignature)) return false;
  for (size_t i = 0; i < sizeof(kPngSignature); ++i) {
    if (blob.bytes()[i] != kPngSignature[i]) return false;
  }
  return true;
}

class SmithyHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<PortraitServer>(std::make_shared<SmithyTracerHandler>());
    loopback_ = std::make_shared<smithy::http::Loopback>();
    ASSERT_TRUE(loopback_->Start(server_->Handler()).ok());
  }

  PortraitClient MakeClient() {
    smithy::ClientConfig config;
    config.http_client = loopback_;
    auto client = PortraitClient::Create(std::move(config));
    EXPECT_TRUE(client.ok()) << client.error().message();
    return std::move(*client);
  }

  std::unique_ptr<PortraitServer> server_;
  std::shared_ptr<smithy::http::Loopback> loopback_;
};

TEST_F(SmithyHandlerTest, TracesSceneToPng) {
  PortraitClient client = MakeClient();
  const auto traced = client.Trace(ValidInput());
  ASSERT_TRUE(traced.ok()) << traced.error().message();
  EXPECT_EQ(traced->width, 20);
  EXPECT_EQ(traced->height, 20);
  EXPECT_TRUE(LooksLikePng(traced->base64_png)) << "blob size: " << traced->base64_png.size();
}

TEST_F(SmithyHandlerTest, IdenticalRequestsServeTheCachedRender) {
  PortraitClient client = MakeClient();
  const auto first = client.Trace(ValidInput());
  ASSERT_TRUE(first.ok()) << first.error().message();
  const auto second = client.Trace(ValidInput());
  ASSERT_TRUE(second.ok()) << second.error().message();
  // The second call is served from TracerService's LRU cache; either way the
  // bytes must be identical for identical scenes.
  EXPECT_EQ(first->base64_png, second->base64_png);
}

// The cross-field rules that constraint traits can't express: each must
// surface as the modeled InvalidSceneError with types.cc's message.
struct CrossFieldCase {
  const char* name;
  void (*mutate)(TraceInput&);
  const char* expected_message;
};

class SmithyHandlerCrossFieldTest : public SmithyHandlerTest,
                                    public ::testing::WithParamInterface<CrossFieldCase> {};

TEST_P(SmithyHandlerCrossFieldTest, RejectsWithInvalidSceneError) {
  TraceInput input = ValidInput();
  GetParam().mutate(input);

  PortraitClient client = MakeClient();
  const auto denied = client.Trace(input);
  ASSERT_FALSE(denied.ok());
  EXPECT_EQ(denied.error().code(), "InvalidSceneError");
  ASSERT_NE(denied.error().detail<InvalidSceneError>(), nullptr);
  EXPECT_EQ(denied.error().detail<InvalidSceneError>()->message, GetParam().expected_message);
}

INSTANTIATE_TEST_SUITE_P(
    CrossFieldRules, SmithyHandlerCrossFieldTest,
    ::testing::Values(
        CrossFieldCase{"CameraAtFocus",
                       [](TraceInput& in) { in.perspective.cameraFocus = {0.0, 0.0, -1.0}; },
                       "Camera position and focus cannot be the same"},
        CrossFieldCase{"ExtremeAspectRatio",
                       [](TraceInput& in) {
                         in.output.width = 1200;
                         in.output.height = 20;
                       },
                       "Aspect ratio too extreme"},
        CrossFieldCase{"ZeroRadius", [](TraceInput& in) { in.scene.spheres[0].radius = 0.0; },
                       "Sphere radius must be positive"}),
    [](const auto& info) { return info.param.name; });

TEST_F(SmithyHandlerTest, ConcurrentTracesAreSafe) {
  // Regression test for the shared tracy::Tracer RNG data race: one handler
  // instance, many threads, a mix of fresh renders and cache hits.
  constexpr int kThreads = 8;
  constexpr int kRequestsPerThread = 4;
  std::vector<std::thread> threads;
  std::vector<int> failures(kThreads, 0);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([this, t, &failures] {
      PortraitClient client = MakeClient();
      for (int i = 0; i < kRequestsPerThread; ++i) {
        // Half the requests collide across threads (cache contention), half
        // are unique to the thread (concurrent fresh renders).
        const double x = (i % 2 == 0) ? 0.5 * i : 0.1 * (t + 1);
        const auto traced = client.Trace(ValidInput(x));
        if (!traced.ok() || !LooksLikePng(traced->base64_png)) ++failures[t];
      }
    });
  }
  for (auto& thread : threads) thread.join();

  for (int t = 0; t < kThreads; ++t) {
    EXPECT_EQ(failures[t], 0) << "thread " << t << " had failing traces";
  }
}

}  // namespace
