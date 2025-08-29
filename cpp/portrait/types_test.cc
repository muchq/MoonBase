#include "cpp/portrait/types.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "absl/status/status.h"

namespace portrait {
namespace {

TEST(TypesValidationTest, ValidateVec3) {
  Vec3 vec3 = {1.0, 2.0, 3.0};
  absl::Status status = validateVec3(vec3);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidatePerspective_Valid) {
  Perspective perspective;
  perspective.cameraPosition = {0.0, 0.0, 0.0};
  perspective.cameraFocus = {0.0, 0.0, 1.0};
  
  absl::Status status = validatePerspective(perspective);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateScene_Empty) {
  Scene scene;
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "empty scene");
}

TEST(TypesValidationTest, ValidateScene_TooManySpheres) {
  Scene scene;
  for (int i = 0; i < 11; ++i) {
    Sphere sphere;
    sphere.center = {static_cast<double>(i), 0.0, 0.0};
    sphere.radius = 1.0;
    sphere.color = {255, 255, 255};
    sphere.specular = 0.5;
    sphere.reflective = 0.3;
    scene.spheres.push_back(sphere);
  }
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "max spheres is 10");
}

TEST(TypesValidationTest, ValidateScene_Valid) {
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateScene_MaxSpheres) {
  Scene scene;
  for (int i = 0; i < 10; ++i) {
    Sphere sphere;
    sphere.center = {static_cast<double>(i), 0.0, 0.0};
    sphere.radius = 1.0;
    sphere.color = {255, 255, 255};
    sphere.specular = 0.5;
    sphere.reflective = 0.3;
    scene.spheres.push_back(sphere);
  }
  
  absl::Status status = validateScene(scene);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateOutput_TooSmallWidth) {
  Output output;
  output.width = 19;
  output.height = 100;
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "min width is 20 pixels");
}

TEST(TypesValidationTest, ValidateOutput_TooSmallHeight) {
  Output output;
  output.width = 100;
  output.height = 19;
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "min height is 20 pixels");
}

TEST(TypesValidationTest, ValidateOutput_TooLargeWidth) {
  Output output;
  output.width = 1201;
  output.height = 100;
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "max width is 1200 pixels");
}

TEST(TypesValidationTest, ValidateOutput_TooLargeHeight) {
  Output output;
  output.width = 100;
  output.height = 1201;
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "max height is 1200 pixels");
}

TEST(TypesValidationTest, ValidateOutput_Valid) {
  Output output;
  output.width = 640;
  output.height = 480;
  
  absl::Status status = validateOutput(output);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateOutput_MinDimensions) {
  Output output;
  output.width = 20;
  output.height = 20;
  
  absl::Status status = validateOutput(output);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateOutput_MaxDimensions) {
  Output output;
  output.width = 1200;
  output.height = 1200;
  
  absl::Status status = validateOutput(output);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateTraceRequest_Valid) {
  TraceRequest request;
  
  request.perspective.cameraPosition = {0.0, 0.0, 0.0};
  request.perspective.cameraFocus = {0.0, 0.0, 1.0};
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 5.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  request.scene.spheres.push_back(sphere);
  
  request.output.width = 640;
  request.output.height = 480;
  
  absl::Status status = validateTraceRequest(request);
  EXPECT_TRUE(status.ok());
}

TEST(TypesValidationTest, ValidateTraceRequest_InvalidScene) {
  TraceRequest request;
  
  request.perspective.cameraPosition = {0.0, 0.0, 0.0};
  request.perspective.cameraFocus = {0.0, 0.0, 1.0};
  
  request.output.width = 640;
  request.output.height = 480;
  
  absl::Status status = validateTraceRequest(request);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "empty scene");
}

TEST(TypesValidationTest, ValidateTraceRequest_InvalidOutput) {
  TraceRequest request;
  
  request.perspective.cameraPosition = {0.0, 0.0, 0.0};
  request.perspective.cameraFocus = {0.0, 0.0, 1.0};
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 5.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  request.scene.spheres.push_back(sphere);
  
  request.output.width = 10;
  request.output.height = 480;
  
  absl::Status status = validateTraceRequest(request);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(), "min width is 20 pixels");
}

TEST(TypesValidationTest, ValidateVec3_NaNInX) {
  Vec3 vec3_nan = {std::nan(""), 2.0, 3.0};
  absl::Status status = validateVec3(vec3_nan);
  EXPECT_FALSE(status.ok()) << "Vec3 with NaN in X should be invalid";
}

TEST(TypesValidationTest, ValidateVec3_NaNInY) {
  Vec3 vec3_nan = {1.0, std::nan(""), 3.0};
  absl::Status status = validateVec3(vec3_nan);
  EXPECT_FALSE(status.ok()) << "Vec3 with NaN in Y should be invalid";
}

TEST(TypesValidationTest, ValidateVec3_NaNInZ) {
  Vec3 vec3_nan = {1.0, 2.0, std::nan("")};
  absl::Status status = validateVec3(vec3_nan);
  EXPECT_FALSE(status.ok()) << "Vec3 with NaN in Z should be invalid";
}

TEST(TypesValidationTest, ValidateVec3_InfiniteInX) {
  Vec3 vec3_inf = {std::numeric_limits<double>::infinity(), 2.0, 3.0};
  absl::Status status = validateVec3(vec3_inf);
  EXPECT_FALSE(status.ok()) << "Vec3 with infinity in X should be invalid";
}

TEST(TypesValidationTest, ValidateVec3_InfiniteInY) {
  Vec3 vec3_inf = {1.0, std::numeric_limits<double>::infinity(), 3.0};
  absl::Status status = validateVec3(vec3_inf);
  EXPECT_FALSE(status.ok()) << "Vec3 with infinity in Y should be invalid";
}

TEST(TypesValidationTest, ValidateVec3_InfiniteInZ) {
  Vec3 vec3_inf = {1.0, 2.0, std::numeric_limits<double>::infinity()};
  absl::Status status = validateVec3(vec3_inf);
  EXPECT_FALSE(status.ok()) << "Vec3 with infinity in Z should be invalid";
}

TEST(TypesValidationTest, ValidateVec3_NegativeInfinity) {
  Vec3 vec3_inf = {-std::numeric_limits<double>::infinity(), 2.0, 3.0};
  absl::Status status = validateVec3(vec3_inf);
  EXPECT_FALSE(status.ok()) << "Vec3 with negative infinity should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NaNCenter) {
  Sphere sphere;
  sphere.center = {std::nan(""), 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with NaN center should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_InfiniteCenter) {
  Sphere sphere;
  sphere.center = {0.0, std::numeric_limits<double>::infinity(), 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with infinite center should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NaNRadius) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = std::nan("");
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with NaN radius should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_InfiniteRadius) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = std::numeric_limits<double>::infinity();
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with infinite radius should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NegativeRadius) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = -1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with negative radius should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_ExcessiveRadius) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1000000.0;  // Unreasonably large radius
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with excessive radius (>10000) should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_ZeroRadius) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 0.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with zero radius should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_InvalidColor) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {256, 0, 0};  // Color value out of range
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with color value > 255 should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NaNSpecular) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = std::nan("");
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with NaN specular should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_InfiniteSpecular) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = std::numeric_limits<double>::infinity();
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with infinite specular should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_ExcessiveSpecular) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 10000.0;  // Unreasonably large specular exponent
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with excessive specular (>1000) should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NegativeSpecular) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = -1.0;
  sphere.reflective = 0.3;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with negative specular should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NaNReflective) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = std::nan("");
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with NaN reflective should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_InfiniteReflective) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = std::numeric_limits<double>::infinity();
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with infinite reflective should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_NegativeReflective) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = -0.1;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with negative reflective should be invalid";
}

TEST(TypesValidationTest, ValidateSphere_ReflectiveGreaterThanOne) {
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 1.1;
  
  Scene scene;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Sphere with reflective > 1.0 should be invalid";
}

TEST(TypesValidationTest, ValidateLight_NaNIntensity) {
  Light light;
  light.lightType = AMBIENT;
  light.intensity = std::nan("");
  light.position = {0.0, 0.0, 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with NaN intensity should be invalid";
}

TEST(TypesValidationTest, ValidateLight_InfiniteIntensity) {
  Light light;
  light.lightType = AMBIENT;
  light.intensity = std::numeric_limits<double>::infinity();
  light.position = {0.0, 0.0, 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with infinite intensity should be invalid";
}

TEST(TypesValidationTest, ValidateLight_ExcessiveIntensity) {
  Light light;
  light.lightType = AMBIENT;
  light.intensity = 100.0;  // Unreasonably bright
  light.position = {0.0, 0.0, 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with excessive intensity (>10) should be invalid";
}

TEST(TypesValidationTest, ValidateLight_NegativeIntensity) {
  Light light;
  light.lightType = AMBIENT;
  light.intensity = -0.5;
  light.position = {0.0, 0.0, 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with negative intensity should be invalid";
}

TEST(TypesValidationTest, ValidateLight_NaNPosition) {
  Light light;
  light.lightType = POINT;
  light.intensity = 0.5;
  light.position = {std::nan(""), 0.0, 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with NaN position should be invalid";
}

TEST(TypesValidationTest, ValidateLight_InfinitePosition) {
  Light light;
  light.lightType = POINT;
  light.intensity = 0.5;
  light.position = {0.0, std::numeric_limits<double>::infinity(), 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with infinite position should be invalid";
}

TEST(TypesValidationTest, ValidateLight_UnknownType) {
  Light light;
  light.lightType = UNKNOWN;
  light.intensity = 0.5;
  light.position = {0.0, 0.0, 0.0};
  
  Scene scene;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  scene.lights.push_back(light);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Light with UNKNOWN type should be invalid";
}

TEST(TypesValidationTest, ValidateScene_InvalidBackgroundColor) {
  Scene scene;
  scene.backgroundColor = {256, 0, 0};  // Invalid color value
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Scene with invalid background color should be invalid";
}

TEST(TypesValidationTest, ValidateScene_NaNStarProbability) {
  Scene scene;
  scene.backgroundStarProbability = std::nan("");
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Scene with NaN star probability should be invalid";
}

TEST(TypesValidationTest, ValidateScene_InfiniteStarProbability) {
  Scene scene;
  scene.backgroundStarProbability = std::numeric_limits<double>::infinity();
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Scene with infinite star probability should be invalid";
}

TEST(TypesValidationTest, ValidateScene_NegativeStarProbability) {
  Scene scene;
  scene.backgroundStarProbability = -0.1;
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Scene with negative star probability should be invalid";
}

TEST(TypesValidationTest, ValidateScene_StarProbabilityGreaterThanOne) {
  Scene scene;
  scene.backgroundStarProbability = 1.1;
  
  Sphere sphere;
  sphere.center = {0.0, 0.0, 0.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 0.5;
  sphere.reflective = 0.3;
  scene.spheres.push_back(sphere);
  
  absl::Status status = validateScene(scene);
  EXPECT_FALSE(status.ok()) << "Scene with star probability > 1.0 should be invalid";
}

TEST(TypesValidationTest, ValidatePerspective_SamePositionAndFocus) {
  Perspective perspective;
  perspective.cameraPosition = {1.0, 2.0, 3.0};
  perspective.cameraFocus = {1.0, 2.0, 3.0};
  
  absl::Status status = validatePerspective(perspective);
  EXPECT_FALSE(status.ok()) << "Camera position and focus should not be the same";
}

TEST(TypesValidationTest, ValidatePerspective_InvalidCameraPosition) {
  Perspective perspective;
  perspective.cameraPosition = {std::nan(""), 0.0, 0.0};
  perspective.cameraFocus = {0.0, 0.0, 1.0};
  
  absl::Status status = validatePerspective(perspective);
  EXPECT_FALSE(status.ok()) << "Camera position with NaN should be invalid";
}

TEST(TypesValidationTest, ValidatePerspective_InvalidCameraFocus) {
  Perspective perspective;
  perspective.cameraPosition = {0.0, 0.0, 0.0};
  perspective.cameraFocus = {std::numeric_limits<double>::infinity(), 0.0, 1.0};
  
  absl::Status status = validatePerspective(perspective);
  EXPECT_FALSE(status.ok()) << "Camera focus with infinity should be invalid";
}

TEST(TypesValidationTest, ValidateOutput_AspectRatioTooExtreme) {
  Output output;
  output.width = 1200;
  output.height = 20;  // Aspect ratio 60:1
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok()) << "Extreme aspect ratio should be invalid";
}

TEST(TypesValidationTest, ValidateOutput_NegativeWidth) {
  Output output;
  output.width = -100;
  output.height = 100;
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok()) << "Negative width should be invalid";
}

TEST(TypesValidationTest, ValidateOutput_NegativeHeight) {
  Output output;
  output.width = 100;
  output.height = -100;
  
  absl::Status status = validateOutput(output);
  EXPECT_FALSE(status.ok()) << "Negative height should be invalid";
}

}  // namespace
}  // namespace portrait