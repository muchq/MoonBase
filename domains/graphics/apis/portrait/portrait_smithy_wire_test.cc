// Wire-compatibility tests: golden JSON fixtures pinning the service's wire
// format are driven through the generated server over the loopback
// transport:
//   - the golden request parses into the generated typed input,
//   - the response body matches the deployed service's shape exactly,
//   - omitted optional scene fields fill their defaults,
//   - every trait-expressible validate* rule from types.cc rejects with 400,
//   - the generated client round-trips, including the modeled error,
//   - each branch of the error space puts the right JSON on the wire, and
//     the server-fault branches put nothing internal there.
// Cross-field rules (camera != focus, aspect ratio, radius > 0) belong to
// the handler and are tested in smithy_handler_test, which asserts the typed
// values a generated client recovers; the error cases here pin the JSON a
// consumer that isn't using the generated client actually receives.

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "domains/graphics/apis/portrait/test_support.h"
#include "moonbase/portrait/client.h"
#include "moonbase/portrait/server.h"
#include "smithy/core/blob.h"
#include "smithy/core/error.h"
#include "smithy/core/outcome.h"
#include "smithy/http/message.h"
#include "smithy/server/router.h"

namespace {

using json = nlohmann::json;
using moonbase::portrait::InvalidSceneError;
using moonbase::portrait::Light;
using moonbase::portrait::LightType;
using moonbase::portrait::PortraitClient;
using moonbase::portrait::PortraitHandler;
using moonbase::portrait::RenderCapacityError;
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

// Consumer-visible router behavior: a request for a path the service
// never modeled is a 404 with the runtime's {"code","message"} envelope.
// Clients (and their monitoring) key off this exact shape to tell "wrong
// URL" apart from an application error.
TEST_F(PortraitWireTest, UnknownRouteReturns404WithCodeEnvelope) {
  smithy::http::HttpRequest request;
  request.method = "POST";
  request.target = "/portrait/v1/nope";
  request.headers.Set("content-type", "application/json");
  request.body = "{}";
  const auto response = harness_.Send(std::move(request));
  EXPECT_EQ(response.status, 404) << response.body;
  EXPECT_EQ(response.headers.Get("content-type").value_or(""), "application/json");
  EXPECT_EQ(response.body, R"({"code":"NotFound","message":"no route matches the request"})");
}

// Consumer-visible router behavior: the right path with the wrong method
// is a 405 whose Allow header lists the methods the route does serve —
// the generated router's promise, distinct from the 404 above.
TEST_F(PortraitWireTest, WrongMethodReturns405WithAllowHeader) {
  smithy::http::HttpRequest request;
  request.method = "GET";
  request.target = "/portrait/v1/trace";
  const auto response = harness_.Send(std::move(request));
  EXPECT_EQ(response.status, 405) << response.body;
  EXPECT_EQ(response.headers.Get("allow").value_or(""), "POST");
  EXPECT_EQ(response.body, R"({"code":"MethodNotAllowed","message":"method not allowed"})");
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

// ---------------------------------------------------------------------------
// The error wire shapes (#1267). The handler tests assert the typed values a
// generated client recovers; these pin the JSON a consumer that isn't using
// the generated client actually receives.

// Emits whatever error a test hands it, so the wire shape of each branch of
// the handler's error space can be pinned without a renderer.
class ErroringHandler final : public PortraitHandler {
 public:
  explicit ErroringHandler(smithy::Error error) : error_(std::move(error)) {}

  smithy::Outcome<TraceOutput> Trace(const TraceInput& /*input*/,
                                     const smithy::server::RequestContext& /*context*/) override {
    return error_;
  }

 private:
  smithy::Error error_;
};

TEST(PortraitErrorWireTest, InvalidSceneErrorCarriesTheOffendingFieldPath) {
  smithy::Error error =
      smithy::Error::Modeled("InvalidSceneError", "Sphere radius must be positive");
  error.set_detail(InvalidSceneError{.message = "Sphere radius must be positive",
                                     .field = "/scene/spheres/0/radius"});
  LoopbackHarness harness{std::make_shared<ErroringHandler>(std::move(error))};

  const auto response = harness.PostTrace(GoldenRequest().dump());

  ASSERT_EQ(response.status, 400) << response.body;
  EXPECT_EQ(response.headers.Get("x-error-type").value_or("<missing>"), "InvalidSceneError");
  // The member name is the contract, not just its presence: a client keys off
  // "field" to locate the problem, and the path form matches the "/member"
  // paths ValidationException uses for the trait-expressible constraints.
  EXPECT_EQ(json::parse(response.body), json({{"message", "Sphere radius must be positive"},
                                              {"field", "/scene/spheres/0/radius"}}));
}

TEST(PortraitErrorWireTest, InvalidSceneErrorOmitsFieldWhenNoSingleMemberIsAtFault) {
  smithy::Error error =
      smithy::Error::Modeled("InvalidSceneError", "Camera position and focus cannot be the same");
  error.set_detail(InvalidSceneError{.message = "Camera position and focus cannot be the same"});
  LoopbackHarness harness{std::make_shared<ErroringHandler>(std::move(error))};

  const auto response = harness.PostTrace(GoldenRequest().dump());

  ASSERT_EQ(response.status, 400) << response.body;
  // Absent, not null or empty — a client can test presence.
  EXPECT_FALSE(json::parse(response.body).contains("field")) << response.body;
}

TEST(PortraitErrorWireTest, RenderCapacityErrorIsA503) {
  smithy::Error error = smithy::Error::Modeled(
      "RenderCapacityError", "render exceeded available memory; try a smaller output",
      /*retryable=*/true);
  error.set_detail(
      RenderCapacityError{.message = "render exceeded available memory; try a smaller output"});
  LoopbackHarness harness{std::make_shared<ErroringHandler>(std::move(error))};

  const auto response = harness.PostTrace(GoldenRequest().dump());

  EXPECT_EQ(response.status, 503) << response.body;
  EXPECT_EQ(response.headers.Get("x-error-type").value_or("<missing>"), "RenderCapacityError");
  EXPECT_EQ(json::parse(response.body),
            json({{"message", "render exceeded available memory; try a smaller output"}}));
}

// The property smithy_handler.cc's "undeclared -> non-leaking 500" comment
// claims. It is the generated server that makes it true — it drops
// error.message() on the kUnknown path — so the test has to exercise the
// server, not the handler.
TEST(PortraitErrorWireTest, AnUnknownErrorsMessageNeverReachesTheClient) {
  constexpr char kSecret[] = "postgres://portrait:hunter2@10.0.0.5/scenes";
  LoopbackHarness harness{
      std::make_shared<ErroringHandler>(smithy::Error::Unknown(std::string(kSecret)))};

  const auto response = harness.PostTrace(GoldenRequest().dump());

  EXPECT_EQ(response.status, 500) << response.body;
  EXPECT_EQ(response.body.find(kSecret), std::string::npos) << response.body;
  EXPECT_EQ(response.body, R"({"__type":"InternalFailure","message":"internal failure"})");
  for (const auto& [name, value] : response.headers.entries()) {
    EXPECT_EQ(value.find(kSecret), std::string::npos) << name << ": " << value;
  }
}

// A modeled code that matches no declared shape does NOT fail loudly: the
// generated ErrorToResponse falls through to a 400 carrying error.message()
// verbatim. So a typo in a code string is simultaneously the wrong status and
// the leak the test above guards against, and the compiler cannot catch it.
//
// Pinned here so the trap is visible in the suite rather than discovered in
// production, and so the "recover it as a typed detail" assertion the other
// error tests make is understood as the thing that rules it out.
TEST(PortraitErrorWireTest, AMisspelledModeledCodeFallsThroughToA400) {
  LoopbackHarness harness{std::make_shared<ErroringHandler>(
      smithy::Error::Modeled("InvalidScenError", "internal detail"))};

  const auto response = harness.PostTrace(GoldenRequest().dump());

  EXPECT_EQ(response.status, 400) << response.body;
  EXPECT_NE(response.body.find("internal detail"), std::string::npos) << response.body;
}

// What the transport does when a handler throws rather than returning an
// error (#1267 finding 1). MoonBase compiles with exceptions enabled, and
// every smithy-cpp server transport — Beast, socket, and the loopback used
// here — routes the handler through InvokeHandlerGuarded, which contains the
// throw as a correlated 500 rather than letting it unwind out of the I/O
// thread and take the process down.
//
// TracerService no longer throws, so nothing in portrait depends on this
// today. It is pinned because the alternative is a crash: if a smithy-cpp
// bump ever removed the guard, the first evidence would otherwise be a
// production restart under load.
class ThrowingHandler final : public PortraitHandler {
 public:
  static constexpr char kSecret[] = "/srv/portrait/oops";

  smithy::Outcome<TraceOutput> Trace(const TraceInput& /*input*/,
                                     const smithy::server::RequestContext& /*context*/) override {
    throw std::runtime_error(kSecret);
  }
};

TEST(PortraitErrorWireTest, AThrowingHandlerIsContainedAsACorrelated500) {
  LoopbackHarness harness{std::make_shared<ThrowingHandler>()};

  const auto response = harness.PostTrace(GoldenRequest().dump());

  EXPECT_EQ(response.status, 500) << response.body;
  EXPECT_EQ(response.body.find(ThrowingHandler::kSecret), std::string::npos) << response.body;

  // The correlation id ties the client's 500 to the server's log line; the
  // guard writes what() to std::clog against the same id.
  const std::string correlation_id = response.headers.Get("x-correlation-id").value_or("");
  EXPECT_FALSE(correlation_id.empty());
  const json body = json::parse(response.body);
  EXPECT_EQ(body.value("message", ""), "internal error");
  EXPECT_EQ(body.value("correlationId", ""), correlation_id);
}

}  // namespace
