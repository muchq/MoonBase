#include "domains/graphics/apis/portrait/tracer_service.h"

#include <gtest/gtest.h>

#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "domains/graphics/apis/portrait/types.h"
#include "domains/graphics/libs/image_core/image_core.h"
#include "domains/graphics/libs/png_plusplus/png_plusplus.h"
#include "domains/platform/libs/futility/otel/capturing_metrics_recorder.h"

namespace portrait {
namespace {

class TracerServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a basic valid scene
    basic_scene_.backgroundColor = {0, 0, 0};
    basic_scene_.backgroundStarProbability = 0.001;

    Sphere sphere1;
    sphere1.center = {0.0, 0.0, 5.0};
    sphere1.radius = 1.0;
    sphere1.color = {255, 0, 0};
    sphere1.specular = 500.0;
    sphere1.reflective = 0.3;
    basic_scene_.spheres.push_back(sphere1);

    Light light1;
    light1.lightType = AMBIENT;
    light1.intensity = 0.2;
    light1.position = {0.0, 0.0, 0.0};
    basic_scene_.lights.push_back(light1);

    Light light2;
    light2.lightType = POINT;
    light2.intensity = 0.6;
    light2.position = {2.0, 1.0, 0.0};
    basic_scene_.lights.push_back(light2);

    // Create a basic perspective
    basic_perspective_.cameraPosition = {0.0, 0.0, 0.0};
    basic_perspective_.cameraFocus = {0.0, 0.0, 1.0};

    // Create a basic output
    basic_output_.width = 100;
    basic_output_.height = 100;
  }

  Scene basic_scene_;
  Perspective basic_perspective_;
  Output basic_output_;
};

TEST_F(TracerServiceTest, TraceValidRequest) {
  TracerService service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  auto result = service.trace(request);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const TraceResponse& response = result.value();
  // Raw PNG bytes, not base64: the handler wraps these in the wire Blob
  // directly.
  EXPECT_TRUE(pngpp::isPng(response.png_bytes));
  EXPECT_EQ(response.width, 100);
  EXPECT_EQ(response.height, 100);
}

TEST_F(TracerServiceTest, TraceWithCache) {
  TracerService service(10);  // Small cache size
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  // First call - should generate image
  auto result1 = service.trace(request);
  ASSERT_TRUE(result1.ok());
  const auto& png_1 = result1.value().png_bytes;

  // Second call with same request - should return cached result
  auto result2 = service.trace(request);
  ASSERT_TRUE(result2.ok());
  const auto& png_2 = result2.value().png_bytes;

  // Cached results should be identical
  EXPECT_EQ(png_1, png_2);
}

TEST_F(TracerServiceTest, TraceInvalidRequestEmptyScene) {
  TracerService service;
  Scene empty_scene;
  TraceRequest request{empty_scene, basic_perspective_, basic_output_};

  auto result = service.trace(request);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "empty scene");
}

TEST_F(TracerServiceTest, TraceInvalidRequestBadOutput) {
  TracerService service;
  Output bad_output{0, 100};  // Invalid width
  TraceRequest request{basic_scene_, basic_perspective_, bad_output};

  auto result = service.trace(request);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TracerServiceTest, TraceDifferentScenes) {
  TracerService service;

  // First request
  TraceRequest request1{basic_scene_, basic_perspective_, basic_output_};
  auto result1 = service.trace(request1);
  ASSERT_TRUE(result1.ok());
  const auto& png_1 = result1.value().png_bytes;

  // Modify scene for second request
  Scene modified_scene = basic_scene_;
  modified_scene.backgroundColor = {255, 255, 255};  // Change background
  TraceRequest request2{modified_scene, basic_perspective_, basic_output_};
  auto result2 = service.trace(request2);
  ASSERT_TRUE(result2.ok());
  const auto& png_2 = result2.value().png_bytes;

  // Different scenes should produce different images
  EXPECT_NE(png_1, png_2);
}

TEST_F(TracerServiceTest, TraceDifferentPerspectives) {
  TracerService service;

  // First request
  TraceRequest request1{basic_scene_, basic_perspective_, basic_output_};
  auto result1 = service.trace(request1);
  ASSERT_TRUE(result1.ok());
  const auto& png_1 = result1.value().png_bytes;

  // Modify perspective for second request
  Perspective modified_perspective = basic_perspective_;
  modified_perspective.cameraPosition = {1.0, 0.0, 0.0};  // Move camera
  TraceRequest request2{basic_scene_, modified_perspective, basic_output_};
  auto result2 = service.trace(request2);
  ASSERT_TRUE(result2.ok());
  const auto& png_2 = result2.value().png_bytes;

  // Different perspectives should produce different images
  EXPECT_NE(png_1, png_2);
}

TEST_F(TracerServiceTest, TraceDifferentOutputSizes) {
  TracerService service;

  // First request with 100x100
  TraceRequest request1{basic_scene_, basic_perspective_, basic_output_};
  auto result1 = service.trace(request1);
  ASSERT_TRUE(result1.ok());
  EXPECT_EQ(result1.value().width, 100);
  EXPECT_EQ(result1.value().height, 100);

  // Second request with 200x150
  Output different_output{200, 150};
  TraceRequest request2{basic_scene_, basic_perspective_, different_output};
  auto result2 = service.trace(request2);
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ(result2.value().width, 200);
  EXPECT_EQ(result2.value().height, 150);

  // Different output sizes should produce different images
  EXPECT_NE(result1.value().png_bytes, result2.value().png_bytes);
}

TEST_F(TracerServiceTest, TraceMultipleSpheres) {
  TracerService service;

  // Add more spheres to the scene
  Scene multi_sphere_scene = basic_scene_;

  Sphere sphere2;
  sphere2.center = {-2.0, 0.0, 6.0};
  sphere2.radius = 0.5;
  sphere2.color = {0, 255, 0};
  sphere2.specular = 100.0;
  sphere2.reflective = 0.1;
  multi_sphere_scene.spheres.push_back(sphere2);

  Sphere sphere3;
  sphere3.center = {2.0, 0.0, 4.0};
  sphere3.radius = 0.75;
  sphere3.color = {0, 0, 255};
  sphere3.specular = 200.0;
  sphere3.reflective = 0.5;
  multi_sphere_scene.spheres.push_back(sphere3);

  TraceRequest request{multi_sphere_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

TEST_F(TracerServiceTest, TraceWithStarBackground) {
  TracerService service;

  Scene starry_scene = basic_scene_;
  starry_scene.backgroundStarProbability = 0.01;  // 1% chance of stars

  TraceRequest request{starry_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

TEST_F(TracerServiceTest, TraceWithDirectionalLight) {
  TracerService service;

  Scene directional_light_scene = basic_scene_;
  Light directional;
  directional.lightType = DIRECTIONAL;
  directional.intensity = 0.8;
  directional.position = {0.0, -1.0, 0.0};  // Light from above
  directional_light_scene.lights.push_back(directional);

  TraceRequest request{directional_light_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

TEST_F(TracerServiceTest, TraceLargeCacheSize) {
  TracerService service(1000);  // Large cache

  // Test that large cache size doesn't cause issues
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

TEST_F(TracerServiceTest, TraceDefaultConstructor) {
  TracerService service;  // Default constructor uses cache size 50

  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

TEST_F(TracerServiceTest, TraceCacheEviction) {
  TracerService service(2);  // Very small cache - only 2 items

  // Create three different scenes
  Scene scene1 = basic_scene_;
  scene1.backgroundColor = {255, 0, 0};

  Scene scene2 = basic_scene_;
  scene2.backgroundColor = {0, 255, 0};

  Scene scene3 = basic_scene_;
  scene3.backgroundColor = {0, 0, 255};

  TraceRequest request1{scene1, basic_perspective_, basic_output_};
  TraceRequest request2{scene2, basic_perspective_, basic_output_};
  TraceRequest request3{scene3, basic_perspective_, basic_output_};

  // Trace all three - should evict the first one
  auto result1 = service.trace(request1);
  ASSERT_TRUE(result1.ok());

  auto result2 = service.trace(request2);
  ASSERT_TRUE(result2.ok());

  auto result3 = service.trace(request3);
  ASSERT_TRUE(result3.ok());

  // Request2 and request3 should still be cached
  auto cached_result2 = service.trace(request2);
  ASSERT_TRUE(cached_result2.ok());
  EXPECT_EQ(result2.value().png_bytes, cached_result2.value().png_bytes);

  auto cached_result3 = service.trace(request3);
  ASSERT_TRUE(cached_result3.ok());
  EXPECT_EQ(result3.value().png_bytes, cached_result3.value().png_bytes);
}

TEST_F(TracerServiceTest, TraceHighReflectiveSphere) {
  TracerService service;

  Scene reflective_scene = basic_scene_;
  reflective_scene.spheres[0].reflective = 0.95;  // Highly reflective

  TraceRequest request{reflective_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

TEST_F(TracerServiceTest, TraceHighSpecularSphere) {
  TracerService service;

  Scene specular_scene = basic_scene_;
  specular_scene.spheres[0].specular = 1000.0;  // High specular value

  TraceRequest request{specular_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().png_bytes.empty());
}

// The render path throws for real — pngpp::imageToPng raises PngException
// from libpng's error handler, and a 1200x1200 Image<RGB_Double> is ~34 MB
// before toRGB() copies it, so std::bad_alloc is reachable under a
// thread-pool transport. trace() used to record a counter and rethrow, which
// left the transport's InvokeHandlerGuarded to answer with a generic 500 the
// handler never got to shape (#1267).
//
// do_trace is the seam because it shares one catch with the PNG encode and
// the cache insert: whichever of them throws takes the same path out. What
// these pin is that no exception escapes trace() and that the status
// distinguishes "ask for less" from "the server broke".
template <typename Thrower>
class ThrowingTracerService : public TracerService {
 public:
  using TracerService::TracerService;

 protected:
  image_core::Image<image_core::RGB_Double> do_trace(Scene&, Perspective&, const Output&) override {
    Thrower::Throw();
    return image_core::Image<image_core::RGB_Double>(1, 1);  // unreachable
  }
};

/// Fails the cache *store*, which runs after the response is already built —
/// a failure mode throwing from do_trace cannot reach.
class FailingCacheWriteService : public TracerService {
 public:
  using TracerService::TracerService;

  int attempts = 0;

 protected:
  void storeInCache(const TraceRequest&, const std::vector<std::uint8_t>&) override {
    ++attempts;
    throw std::bad_alloc();
  }
};

/// Fails the cache *lookup*, which runs before the render and outside what
/// the original try block covered.
class FailingCacheReadService : public TracerService {
 public:
  using TracerService::TracerService;

 protected:
  std::optional<std::vector<std::uint8_t>> lookupCache(const TraceRequest&) override {
    throw std::bad_alloc();
  }
};

struct ThrowBadAlloc {
  static void Throw() { throw std::bad_alloc(); }
};
struct ThrowPngException {
  static void Throw() { throw pngpp::PngException("PNG Error: /srv/secret/path is unreadable"); }
};
struct ThrowNonStdException {
  // Not derived from std::exception: only catch(...) sees this one.
  static void Throw() { throw 42; }
};

TEST_F(TracerServiceTest, OutOfMemoryIsResourceExhaustedRatherThanAThrow) {
  ThrowingTracerService<ThrowBadAlloc> service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::StatusOr<TraceResponse> result = service.trace(request);

  ASSERT_FALSE(result.ok());
  // kResourceExhausted, specifically: it is the one render failure the caller
  // can act on, and the handler maps it to a retryable 503 rather than a 500.
  EXPECT_EQ(result.status().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_EQ(result.status().message(), "render exceeded available memory; try a smaller output");
}

TEST_F(TracerServiceTest, RenderExceptionIsInternalRatherThanAThrow) {
  ThrowingTracerService<ThrowPngException> service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::StatusOr<TraceResponse> result = service.trace(request);

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  // what() goes to the log, not into the status: the status message is the
  // handler's raw material, and this one carries a filesystem path.
  EXPECT_EQ(result.status().message(), "render failed");
  EXPECT_EQ(result.status().message().find("/srv/secret/path"), std::string_view::npos);
}

TEST_F(TracerServiceTest, NonStdExceptionIsContainedToo) {
  ThrowingTracerService<ThrowNonStdException> service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::StatusOr<TraceResponse> result = service.trace(request);

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

// A failed render must not be cached: the next identical request has to try
// again rather than being served the failure forever.
TEST_F(TracerServiceTest, AFailedRenderIsNotCached) {
  ThrowingTracerService<ThrowBadAlloc> service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  ASSERT_FALSE(service.trace(request).ok());
  absl::StatusOr<TraceResponse> second = service.trace(request);

  // Still an error rather than a cache hit — and still the same error, which
  // is what distinguishes "retried and failed" from "served a stale entry".
  ASSERT_FALSE(second.ok());
  EXPECT_EQ(second.status().code(), absl::StatusCode::kResourceExhausted);
}

// A render that succeeded must be returned even if storing it fails. The
// cache is an optimization; reporting "out of memory, try a smaller output"
// here would be a lie about work that already completed, and the smaller
// retry would re-run a render that never needed to.
//
// This is the peak-allocation instant of the whole call — image, png_bytes,
// the response's copy and the cache's copy are all live — so it is the most
// likely allocation in trace() to fail, not a corner case.
TEST_F(TracerServiceTest, ACacheWriteFailureDoesNotDiscardTheRender) {
  FailingCacheWriteService service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::StatusOr<TraceResponse> result = service.trace(request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_TRUE(pngpp::isPng(result->png_bytes));
  // And it really did try to store — otherwise this passes because the code
  // stopped caching altogether.
  EXPECT_EQ(service.attempts, 1);
}

// The cache lookup copies the stored PNG, so the cheap path allocates too.
// Before the guard covered the whole body this unwound out of trace(),
// past the handler, and reached the transport — a different 500 than the one
// the handler shapes, with neither failure counter incremented.
TEST_F(TracerServiceTest, ACacheReadFailureIsAStatusRatherThanAThrow) {
  auto metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>("portrait_test");
  FailingCacheReadService service(50, metrics);
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::StatusOr<TraceResponse> result = service.trace(request);

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_EQ(metrics->CounterTotal("trace_requests_failed", {{"error", "out_of_memory"}}), 1);
}

// The label is the operator's only way to tell an out-of-memory render from
// any other failure, so it needs an assertion that fails when it is wrong —
// including the negative half, or "both branches record the same label"
// passes.
TEST_F(TracerServiceTest, OutOfMemoryAndOtherFailuresAreCountedApart) {
  auto oom_metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>("portrait_test");
  ThrowingTracerService<ThrowBadAlloc> oom(50, oom_metrics);
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};
  ASSERT_FALSE(oom.trace(request).ok());

  EXPECT_EQ(oom_metrics->CounterTotal("trace_requests_failed", {{"error", "out_of_memory"}}), 1);
  EXPECT_EQ(oom_metrics->CounterTotal("trace_requests_failed", {{"error", "rendering_failed"}}), 0);

  auto png_metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>("portrait_test");
  ThrowingTracerService<ThrowPngException> failed(50, png_metrics);
  ASSERT_FALSE(failed.trace(request).ok());

  EXPECT_EQ(png_metrics->CounterTotal("trace_requests_failed", {{"error", "rendering_failed"}}), 1);
  EXPECT_EQ(png_metrics->CounterTotal("trace_requests_failed", {{"error", "out_of_memory"}}), 0);
}

// The positive twin of the leak tests. Those pin that what() never reaches
// the status or the wire; this pins that it still reaches the operator.
// Without it, deleting `<< e.what()` leaves the suite green while erasing
// the only diagnostic for every non-OOM render failure.
TEST_F(TracerServiceTest, TheCauseOfARenderFailureIsLogged) {
  ThrowingTracerService<ThrowPngException> service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::ScopedMockLog log;
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, testing::_, testing::HasSubstr("/srv/secret/path")))
      .Times(1);
  log.StartCapturingLogs();
  ASSERT_FALSE(service.trace(request).ok());
  log.StopCapturingLogs();
}

// The control for the three tests above: the same subclass mechanism with a
// renderer that does not throw still succeeds. Without this, a
// ThrowingTracerService that silently failed to override do_trace — or a
// trace() that returned an error before ever reaching the try block — would
// make all of them pass for the wrong reason.
class PlainSubclassTracerService : public TracerService {
 public:
  using TracerService::TracerService;
};

TEST_F(TracerServiceTest, SubclassingAloneDoesNotBreakTheRender) {
  PlainSubclassTracerService service;
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  absl::StatusOr<TraceResponse> result = service.trace(request);

  ASSERT_TRUE(result.ok()) << result.status().message();
  EXPECT_TRUE(pngpp::isPng(result->png_bytes));
}

// --- Cache metrics (#1211) ---------------------------------------------
// The service no longer counts its own cache outcomes; it gets them by
// holding an aura::Cache. These assert the standard family arrives through
// the real service, with portrait's labels, rather than only through the
// wrapper's own unit tests.

/// The service these cache tests speak for. The cache reads its
/// service_name label off the recorder, so a recorder built for some other
/// name would emit some other label — which is the property that keeps the
/// two from drifting, and the reason this is one constant here too.
constexpr const char* kService = "portrait";

/// The labels every portrait cache series carries.
std::map<std::string, std::string> TraceCacheLabels() {
  return {{"service_name", kService}, {"cache", "trace"}};
}

TEST_F(TracerServiceTest, TheFirstRenderIsAMissAndTheRepeatIsAHit) {
  auto metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>(kService);
  TracerService service(10, metrics);
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  ASSERT_TRUE(service.trace(request).ok());
  EXPECT_EQ(metrics->CounterTotal("cache_misses", TraceCacheLabels()), 1);
  EXPECT_EQ(metrics->CounterTotal("cache_hits", TraceCacheLabels()), 0);

  ASSERT_TRUE(service.trace(request).ok());
  EXPECT_EQ(metrics->CounterTotal("cache_hits", TraceCacheLabels()), 1);
  EXPECT_EQ(metrics->CounterTotal("cache_misses", TraceCacheLabels()), 1)
      << "the second, identical request missed";
}

TEST_F(TracerServiceTest, ADifferentSceneMissesRatherThanReusingTheLastRender) {
  auto metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>(kService);
  TracerService service(10, metrics);
  TraceRequest first{basic_scene_, basic_perspective_, basic_output_};

  Scene other_scene = basic_scene_;
  other_scene.spheres[0].center = {1.0, 0.0, 5.0};
  TraceRequest second{other_scene, basic_perspective_, basic_output_};

  ASSERT_TRUE(service.trace(first).ok());
  ASSERT_TRUE(service.trace(second).ok());

  EXPECT_EQ(metrics->CounterTotal("cache_misses", TraceCacheLabels()), 2);
  EXPECT_EQ(metrics->CounterTotal("cache_hits", TraceCacheLabels()), 0);
}

/// The labels scene complexity carries on each path.
std::map<std::string, std::string> SceneLabels(bool cache_hit) {
  return {{"cache_hit", cache_hit ? "true" : "false"}};
}

/// How many observations of `name` landed under exactly these labels — the
/// count, where CounterTotal gives the sum. A mean needs both.
int ObservationCount(const futility::otel::CapturingMetricsRecorder& metrics,
                     const std::string& name, const std::map<std::string, std::string>& labels) {
  int found = 0;
  for (const futility::otel::CapturingMetricsRecorder::Entry& entry : metrics.Entries()) {
    if (entry.name == name && entry.attributes == labels) ++found;
  }
  return found;
}

TEST_F(TracerServiceTest, SceneComplexityIsRecordedOnTheCacheHitPathToo) {
  auto metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>(kService);
  TracerService service(10, metrics);
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  ASSERT_TRUE(service.trace(request).ok());
  ASSERT_TRUE(service.trace(request).ok());

  EXPECT_EQ(ObservationCount(*metrics, "scene_sphere_count", SceneLabels(false)), 1);
  EXPECT_EQ(ObservationCount(*metrics, "scene_sphere_count", SceneLabels(true)), 1)
      << "the cached request never reached the recorder";
  EXPECT_EQ(ObservationCount(*metrics, "scene_light_count", SceneLabels(true)), 1);

  // basic_scene_ is one sphere and two lights, on both paths.
  EXPECT_EQ(metrics->CounterTotal("scene_sphere_count", SceneLabels(true)), 1);
  EXPECT_EQ(metrics->CounterTotal("scene_light_count", SceneLabels(true)), 2);
}

/**
 * The #1287 reading, in miniature: a workload whose two means differ.
 *
 * Four requests over two scenes — a 1-sphere scene asked for three times and
 * rendered once, and a 5-sphere scene rendered once. Offered load is 8 spheres
 * over 4 requests (2.0); render cost is 6 spheres over 2 renders (3.0). Before
 * the cache-hit path recorded anything, the panel labelled `avg_spheres_1h`
 * could only ever answer 3.0.
 *
 * The ratios are chosen so the two disagree, which the issue's own first batch
 * did not manage: 33/11 and 9/3 are both exactly 3.0, so a homogeneous workload
 * cannot tell the readings apart and neither could a test built on one.
 */
TEST_F(TracerServiceTest, ComplexityDistinguishesOfferedLoadFromRenderCost) {
  auto metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>(kService);
  TracerService service(10, metrics);

  TraceRequest light{basic_scene_, basic_perspective_, basic_output_};
  ASSERT_TRUE(service.trace(light).ok());  // miss
  ASSERT_TRUE(service.trace(light).ok());  // hit
  ASSERT_TRUE(service.trace(light).ok());  // hit

  Scene heavy_scene = basic_scene_;
  for (int i = 0; i < 4; i++) {
    Sphere extra;
    extra.center = {static_cast<double>(i) + 2.0, 0.0, 5.0};
    extra.radius = 0.5;
    extra.color = {0, 0, 255};
    extra.specular = 100.0;
    extra.reflective = 0.1;
    heavy_scene.spheres.push_back(extra);
  }
  TraceRequest heavy{heavy_scene, basic_perspective_, basic_output_};
  ASSERT_TRUE(service.trace(heavy).ok());  // miss

  const double rendered_spheres = metrics->CounterTotal("scene_sphere_count", SceneLabels(false));
  const int renders = ObservationCount(*metrics, "scene_sphere_count", SceneLabels(false));
  const double requested_spheres =
      rendered_spheres + metrics->CounterTotal("scene_sphere_count", SceneLabels(true));
  const int requests =
      renders + ObservationCount(*metrics, "scene_sphere_count", SceneLabels(true));

  EXPECT_EQ(renders, 2);
  EXPECT_EQ(requests, 4);
  EXPECT_EQ(rendered_spheres, 6.0);
  EXPECT_EQ(requested_spheres, 8.0);

  // Both readings are now available, and they are different numbers — which is
  // the whole point of carrying the label.
  EXPECT_EQ(requested_spheres / requests, 2.0) << "offered load: what callers asked to render";
  EXPECT_EQ(rendered_spheres / renders, 3.0) << "render cost: what the tracer actually drew";
  EXPECT_NE(requested_spheres / requests, rendered_spheres / renders);
}

TEST_F(TracerServiceTest, TheBespokeTraceCacheCountersAreGone) {
  auto metrics = std::make_shared<futility::otel::CapturingMetricsRecorder>(kService);
  TracerService service(10, metrics);
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  ASSERT_TRUE(service.trace(request).ok());
  ASSERT_TRUE(service.trace(request).ok());

  // prom_proxy now queries the standard family. A migration that emitted
  // both names would satisfy the assertions above while leaving the old
  // series to rot, so the absence is its own assertion.
  //
  // Scanned by name over every entry rather than through CounterTotal:
  // CounterTotal matches attributes exactly, so asking it for the bare
  // "trace_cache_hits" says nothing about a resurrected counter that carries
  // any label at all. That is not hypothetical — it is the form the counter
  // would take if it came back beside the new one.
  for (const futility::otel::CapturingMetricsRecorder::Entry& entry : metrics->Entries()) {
    EXPECT_EQ(entry.name.find("trace_cache_"), std::string::npos)
        << "the bespoke counter is back, labelled: " << entry.name;
  }
}

}  // namespace
}  // namespace portrait
