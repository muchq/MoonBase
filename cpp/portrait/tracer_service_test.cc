#include "cpp/portrait/tracer_service.h"

#include <gtest/gtest.h>

#include <vector>

#include "absl/status/status.h"
#include "cpp/portrait/types.h"

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
  EXPECT_FALSE(response.base64_png.empty());
  EXPECT_EQ(response.width, 100);
  EXPECT_EQ(response.height, 100);
}

TEST_F(TracerServiceTest, TraceWithCache) {
  TracerService service(10);  // Small cache size
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};

  // First call - should generate image
  auto result1 = service.trace(request);
  ASSERT_TRUE(result1.ok());
  const std::string& base64_1 = result1.value().base64_png;

  // Second call with same request - should return cached result
  auto result2 = service.trace(request);
  ASSERT_TRUE(result2.ok());
  const std::string& base64_2 = result2.value().base64_png;

  // Cached results should be identical
  EXPECT_EQ(base64_1, base64_2);
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
  const std::string& base64_1 = result1.value().base64_png;

  // Modify scene for second request
  Scene modified_scene = basic_scene_;
  modified_scene.backgroundColor = {255, 255, 255};  // Change background
  TraceRequest request2{modified_scene, basic_perspective_, basic_output_};
  auto result2 = service.trace(request2);
  ASSERT_TRUE(result2.ok());
  const std::string& base64_2 = result2.value().base64_png;

  // Different scenes should produce different images
  EXPECT_NE(base64_1, base64_2);
}

TEST_F(TracerServiceTest, TraceDifferentPerspectives) {
  TracerService service;

  // First request
  TraceRequest request1{basic_scene_, basic_perspective_, basic_output_};
  auto result1 = service.trace(request1);
  ASSERT_TRUE(result1.ok());
  const std::string& base64_1 = result1.value().base64_png;

  // Modify perspective for second request
  Perspective modified_perspective = basic_perspective_;
  modified_perspective.cameraPosition = {1.0, 0.0, 0.0};  // Move camera
  TraceRequest request2{basic_scene_, modified_perspective, basic_output_};
  auto result2 = service.trace(request2);
  ASSERT_TRUE(result2.ok());
  const std::string& base64_2 = result2.value().base64_png;

  // Different perspectives should produce different images
  EXPECT_NE(base64_1, base64_2);
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
  EXPECT_NE(result1.value().base64_png, result2.value().base64_png);
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
  EXPECT_FALSE(result.value().base64_png.empty());
}

TEST_F(TracerServiceTest, TraceWithStarBackground) {
  TracerService service;

  Scene starry_scene = basic_scene_;
  starry_scene.backgroundStarProbability = 0.01;  // 1% chance of stars

  TraceRequest request{starry_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().base64_png.empty());
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
  EXPECT_FALSE(result.value().base64_png.empty());
}

TEST_F(TracerServiceTest, TraceLargeCacheSize) {
  TracerService service(1000);  // Large cache

  // Test that large cache size doesn't cause issues
  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().base64_png.empty());
}

TEST_F(TracerServiceTest, TraceDefaultConstructor) {
  TracerService service;  // Default constructor uses cache size 50

  TraceRequest request{basic_scene_, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().base64_png.empty());
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
  EXPECT_EQ(result2.value().base64_png, cached_result2.value().base64_png);

  auto cached_result3 = service.trace(request3);
  ASSERT_TRUE(cached_result3.ok());
  EXPECT_EQ(result3.value().base64_png, cached_result3.value().base64_png);
}

TEST_F(TracerServiceTest, TraceHighReflectiveSphere) {
  TracerService service;

  Scene reflective_scene = basic_scene_;
  reflective_scene.spheres[0].reflective = 0.95;  // Highly reflective

  TraceRequest request{reflective_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().base64_png.empty());
}

TEST_F(TracerServiceTest, TraceHighSpecularSphere) {
  TracerService service;

  Scene specular_scene = basic_scene_;
  specular_scene.spheres[0].specular = 1000.0;  // High specular value

  TraceRequest request{specular_scene, basic_perspective_, basic_output_};
  auto result = service.trace(request);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result.value().base64_png.empty());
}

}  // namespace
}  // namespace portrait