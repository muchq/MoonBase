// Wire-compatibility tests: golden JSON fixtures pinning the service's wire
// format are driven through the generated server over the loopback
// transport:
//   - the golden request parses into the generated typed input,
//   - the response body matches the deployed service's shape exactly,
//   - omitted optional scene fields fill their defaults,
//   - every trait-expressible validate* rule from types.cc rejects with 400,
//   - the generated client round-trips, including the modeled error.
// Cross-field rules (camera != focus, aspect ratio, radius > 0) belong to
// the handler and are tested in smithy_handler_test.

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/graphics/apis/portrait/test_support.h"
#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/core/blob.h"

namespace {

using json = nlohmann::json;
using moonbase::portrait::InvalidSceneError;
using moonbase::portrait::Light;
using moonbase::portrait::LightType;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::PortraitHandler;
using moonbase::portrait::Sphere;
using moonbase::portrait::TraceInput;
using moonbase::portrait::TraceOutput;
using portrait::test_support::LoopbackHarness;

constexpr char kFakePng[] = "not-really-a-png";
constexpr char kFakePngBase64[] = "bm90LXJlYWxseS1hLXBuZw==";

// Records the parsed input and echoes the requested dimensions, so tests can
// assert both directions of the wire without any rendering.
class RecordingHandler final : public PortraitHandler {
 public:
  smithy::Outcome<TraceOutput> Trace(const TraceInput& input,
                                     const smithy::server::RequestContext& /*context*/) override {
    const std::lock_guard<std::mutex> lock(mu_);
    last_input_ = input;
    if (reject_scene_) {
      const std::string message = "camera position and focus cannot be the same";
      smithy::Error error = smithy::Error::Modeled("InvalidSceneError", message);
      error.set_detail(InvalidSceneError{.message = message});
      return error;
    }
    TraceOutput output;
    output.base64_png = smithy::Blob::FromString(kFakePng);
    output.width = input.output.width;
    output.height = input.output.height;
    return output;
  }

  std::optional<TraceInput> last_input() {
    const std::lock_guard<std::mutex> lock(mu_);
    return last_input_;
  }

  void reject_scene(bool reject) {
    const std::lock_guard<std::mutex> lock(mu_);
    reject_scene_ = reject;
  }

 private:
  std::mutex mu_;
  std::optional<TraceInput> last_input_;
  bool reject_scene_ = false;
};

// The golden wire format (tuples as JSON arrays, camelCase field names),
// richer than the canonical test_support scene so optional fields are
// pinned too.
json GoldenRequest() {
  return json::parse(R"({
    "scene": {
      "backgroundColor": [10, 20, 30],
      "backgroundStarProbability": 0.05,
      "spheres": [
        {"center": [0.0, -1.0, 3.0], "radius": 1.0, "color": [255, 0, 0],
         "specular": 500.0, "reflective": 0.2},
        {"center": [2.0, 0.0, 4.0], "radius": 1.0, "color": [0, 0, 255],
         "specular": 500.0, "reflective": 0.3}
      ],
      "lights": [
        {"lightType": "ambient", "intensity": 0.2, "position": [0.0, 0.0, 0.0]},
        {"lightType": "point", "intensity": 0.6, "position": [2.0, 1.0, 0.0]}
      ]
    },
    "perspective": {
      "cameraPosition": [0.0, 0.0, -1.0],
      "cameraFocus": [0.0, 0.0, 0.0]
    },
    "output": {"width": 320, "height": 240}
  })");
}

class PortraitWireTest : public ::testing::Test {
 protected:
  std::shared_ptr<RecordingHandler> handler_ = std::make_shared<RecordingHandler>();
  LoopbackHarness harness_{handler_};
};

TEST_F(PortraitWireTest, GoldenRequestParsesIntoTypedInput) {
  const auto response = harness_.PostTrace(GoldenRequest().dump());
  ASSERT_EQ(response.status, 200) << response.body;

  const auto input = handler_->last_input();
  ASSERT_TRUE(input.has_value());

  ASSERT_TRUE(input->scene.backgroundColor.has_value());
  EXPECT_EQ(*input->scene.backgroundColor, (std::vector<int32_t>{10, 20, 30}));
  EXPECT_DOUBLE_EQ(input->scene.backgroundStarProbability, 0.05);

  ASSERT_EQ(input->scene.spheres.size(), 2u);
  const Sphere& sphere = input->scene.spheres[0];
  EXPECT_EQ(sphere.center, (std::vector<double>{0.0, -1.0, 3.0}));
  EXPECT_DOUBLE_EQ(sphere.radius, 1.0);
  EXPECT_EQ(sphere.color, (std::vector<int32_t>{255, 0, 0}));
  EXPECT_DOUBLE_EQ(sphere.specular, 500.0);
  EXPECT_DOUBLE_EQ(sphere.reflective, 0.2);

  ASSERT_EQ(input->scene.lights.size(), 2u);
  EXPECT_EQ(input->scene.lights[0].lightType, LightType::Value::kAmbient);
  EXPECT_DOUBLE_EQ(input->scene.lights[0].intensity, 0.2);
  EXPECT_EQ(input->scene.lights[1].lightType, LightType::Value::kPoint);
  EXPECT_EQ(input->scene.lights[1].position, (std::vector<double>{2.0, 1.0, 0.0}));

  EXPECT_EQ(input->perspective.cameraPosition, (std::vector<double>{0.0, 0.0, -1.0}));
  EXPECT_EQ(input->perspective.cameraFocus, (std::vector<double>{0.0, 0.0, 0.0}));
  EXPECT_EQ(input->output.width, 320);
  EXPECT_EQ(input->output.height, 240);
}

TEST_F(PortraitWireTest, ResponseMatchesCurrentWireShape) {
  const auto response = harness_.PostTrace(GoldenRequest().dump());
  ASSERT_EQ(response.status, 200) << response.body;
  EXPECT_EQ(response.headers.Get("content-type").value_or(""), "application/json");

  // Field names and value encodings must match what the service emits
  // today: base64_png as standard base64, plain integer dimensions.
  const json expected = {{"base64_png", kFakePngBase64}, {"width", 320}, {"height", 240}};
  EXPECT_EQ(json::parse(response.body), expected);
}

TEST_F(PortraitWireTest, OmittedOptionalSceneFieldsFillDefaults) {
  json request = GoldenRequest();
  request["scene"].erase("backgroundColor");
  request["scene"].erase("backgroundStarProbability");
  request["scene"].erase("lights");

  const auto response = harness_.PostTrace(request.dump());
  ASSERT_EQ(response.status, 200) << response.body;

  const auto input = handler_->last_input();
  ASSERT_TRUE(input.has_value());
  EXPECT_FALSE(input->scene.backgroundColor.has_value());
  EXPECT_DOUBLE_EQ(input->scene.backgroundStarProbability, 0.0);
  EXPECT_TRUE(input->scene.lights.empty());
}

// Every trait-expressible rule from portrait/types.cc, as a table of golden
// mutations: 400 ValidationException with the offending member named.
struct ConstraintCase {
  const char* name;
  void (*mutate)(json&);
  const char* expect_in_body;
};

class PortraitConstraintTest : public PortraitWireTest,
                               public ::testing::WithParamInterface<ConstraintCase> {};

TEST_P(PortraitConstraintTest, RejectsWith400) {
  json request = GoldenRequest();
  GetParam().mutate(request);

  const auto response = harness_.PostTrace(request.dump());
  EXPECT_EQ(response.status, 400) << response.body;
  EXPECT_EQ(response.headers.Get("x-error-type").value_or("<missing>"), "ValidationException")
      << response.body;
  // The fieldList entries name the offending member in their path/message.
  EXPECT_NE(response.body.find(GetParam().expect_in_body), std::string::npos) << response.body;

  // The handler must never see a request that fails constraint validation.
  EXPECT_FALSE(handler_->last_input().has_value());
}

INSTANTIATE_TEST_SUITE_P(
    TypesCcRules, PortraitConstraintTest,
    ::testing::Values(
        ConstraintCase{"EmptyScene", [](json& r) { r["scene"]["spheres"] = json::array(); },
                       "spheres"},
        ConstraintCase{"TooManySpheres",
                       [](json& r) {
                         const json sphere = r["scene"]["spheres"][0];
                         for (int i = 0; i < 11; ++i) r["scene"]["spheres"][i] = sphere;
                       },
                       "spheres"},
        ConstraintCase{"RadiusTooLarge",
                       [](json& r) { r["scene"]["spheres"][0]["radius"] = 20000.0; }, "radius"},
        ConstraintCase{"SpecularNegative",
                       [](json& r) { r["scene"]["spheres"][0]["specular"] = -1.0; }, "specular"},
        ConstraintCase{"ReflectiveAboveOne",
                       [](json& r) { r["scene"]["spheres"][0]["reflective"] = 1.5; }, "reflective"},
        ConstraintCase{"ColorChannelAbove255",
                       [](json& r) { r["scene"]["spheres"][0]["color"][0] = 300; }, "color"},
        ConstraintCase{"CenterNotThreeElements",
                       [](json& r) { r["scene"]["spheres"][0]["center"] = {1.0, 2.0}; }, "center"},
        ConstraintCase{"UnknownLightType",
                       [](json& r) { r["scene"]["lights"][0]["lightType"] = "spot"; }, "lightType"},
        ConstraintCase{"IntensityTooHigh",
                       [](json& r) { r["scene"]["lights"][0]["intensity"] = 11.0; }, "intensity"},
        ConstraintCase{"StarProbabilityAboveOne",
                       [](json& r) { r["scene"]["backgroundStarProbability"] = 1.5; },
                       "backgroundStarProbability"},
        ConstraintCase{"WidthTooSmall", [](json& r) { r["output"]["width"] = 10; }, "width"},
        ConstraintCase{"HeightTooLarge", [](json& r) { r["output"]["height"] = 5000; }, "height"},
        ConstraintCase{"MissingPerspective", [](json& r) { r.erase("perspective"); },
                       "perspective"}),
    [](const auto& info) { return info.param.name; });

TEST_F(PortraitWireTest, NonJsonContentTypeRejected) {
  const auto response = harness_.PostTrace(GoldenRequest().dump(), "text/plain");
  EXPECT_EQ(response.status, 415) << response.body;
}

TEST_F(PortraitWireTest, GeneratedClientRoundTrips) {
  PortraitClient client = harness_.MakeClient();

  Sphere sphere;
  sphere.center = {0.0, -1.0, 3.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 500.0;
  sphere.reflective = 0.2;

  Light light;
  light.lightType = LightType::Value::kDirectional;
  light.intensity = 0.4;
  light.position = {1.0, 4.0, 4.0};

  TraceInput input;
  input.scene.spheres = {sphere};
  input.scene.lights = {light};
  input.perspective.cameraPosition = {0.0, 0.0, -1.0};
  input.perspective.cameraFocus = {0.0, 0.0, 0.0};
  input.output.width = 640;
  input.output.height = 480;

  const auto traced = client.Trace(input);
  ASSERT_TRUE(traced.ok()) << traced.error().message();
  EXPECT_EQ(traced->base64_png, smithy::Blob::FromString(kFakePng));
  EXPECT_EQ(traced->width, 640);
  EXPECT_EQ(traced->height, 480);

  const auto parsed = handler_->last_input();
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->scene.lights[0].lightType, LightType::Value::kDirectional);
  EXPECT_FALSE(parsed->scene.backgroundColor.has_value());
}

TEST_F(PortraitWireTest, ModeledErrorSurfacesTyped) {
  handler_->reject_scene(true);

  PortraitClient client = harness_.MakeClient();

  TraceInput input;
  Sphere sphere;
  sphere.center = {0.0, 0.0, 3.0};
  sphere.radius = 1.0;
  sphere.color = {1, 2, 3};
  sphere.specular = 0.0;
  sphere.reflective = 0.0;
  input.scene.spheres = {sphere};
  input.perspective.cameraPosition = {0.0, 0.0, 0.0};
  input.perspective.cameraFocus = {0.0, 0.0, 0.0};
  input.output.width = 100;
  input.output.height = 100;

  const auto denied = client.Trace(input);
  ASSERT_FALSE(denied.ok());
  EXPECT_EQ(denied.error().code(), "InvalidSceneError");
  ASSERT_NE(denied.error().detail<InvalidSceneError>(), nullptr);
  EXPECT_EQ(denied.error().detail<InvalidSceneError>()->message,
            "camera position and focus cannot be the same");
}

}  // namespace
