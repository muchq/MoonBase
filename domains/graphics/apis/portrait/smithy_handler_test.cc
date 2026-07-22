// Handler tests: the generated client drives SmithyTracerHandler — real ray
// tracing — over loopback. Covers the happy path (a decodable PNG comes
// back), cache reuse, each cross-field rule surfacing as the modeled
// InvalidSceneError, and a concurrency hammer that regression-tests the
// tracy::Tracer RNG fix (run it under TSan to prove the data race is gone:
//   bazel test --copt=-fsanitize=thread --linkopt=-fsanitize=thread \
//     //domains/graphics/apis/portrait:smithy_handler_test).

#include "domains/graphics/apis/portrait/smithy_handler.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "domains/graphics/apis/portrait/test_support.h"
#include "domains/graphics/libs/png_plusplus/png_plusplus.h"
#include "moonbase/portrait/client.h"

namespace {

using moonbase::portrait::InvalidSceneError;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::TraceInput;
using portrait::SmithyTracerHandler;
using portrait::test_support::LoopbackHarness;
using portrait::test_support::ValidTraceInput;

bool LooksLikePng(const smithy::Blob& blob) { return pngpp::isPng(blob.data(), blob.size()); }

class SmithyHandlerTest : public ::testing::Test {
 protected:
  LoopbackHarness harness_{std::make_shared<SmithyTracerHandler>()};
};

TEST_F(SmithyHandlerTest, TracesSceneToPng) {
  PortraitClient client = harness_.MakeClient();
  const auto traced = client.Trace(ValidTraceInput());
  ASSERT_TRUE(traced.ok()) << traced.error().message();
  EXPECT_EQ(traced->width, 20);
  EXPECT_EQ(traced->height, 20);
  EXPECT_TRUE(LooksLikePng(traced->base64_png)) << "blob size: " << traced->base64_png.size();
}

TEST_F(SmithyHandlerTest, IdenticalRequestsServeTheCachedRender) {
  PortraitClient client = harness_.MakeClient();
  const auto first = client.Trace(ValidTraceInput());
  ASSERT_TRUE(first.ok()) << first.error().message();
  const auto second = client.Trace(ValidTraceInput());
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
  TraceInput input = ValidTraceInput();
  GetParam().mutate(input);

  PortraitClient client = harness_.MakeClient();
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
      PortraitClient client = harness_.MakeClient();
      for (int i = 0; i < kRequestsPerThread; ++i) {
        // Half the requests collide across threads (cache contention), half
        // are unique to the thread (concurrent fresh renders).
        const double x = (i % 2 == 0) ? 0.5 * i : 0.1 * (t + 1);
        const auto traced = client.Trace(ValidTraceInput(x));
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
