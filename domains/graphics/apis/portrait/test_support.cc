#include "domains/graphics/apis/portrait/test_support.h"

#include <gtest/gtest.h>

#include "smithy/client/config.h"

namespace portrait::test_support {

moonbase::portrait::TraceInput ValidTraceInput(double sphere_x) {
  moonbase::portrait::Sphere sphere;
  sphere.center = {sphere_x, -1.0, 3.0};
  sphere.radius = 1.0;
  sphere.color = {255, 0, 0};
  sphere.specular = 500.0;
  sphere.reflective = 0.2;

  moonbase::portrait::Light light;
  light.lightType = moonbase::portrait::LightType::Value::kAmbient;
  light.intensity = 0.4;
  light.position = {0.0, 0.0, 0.0};

  moonbase::portrait::TraceInput input;
  input.scene.spheres = {sphere};
  input.scene.lights = {light};
  input.perspective.cameraPosition = {0.0, 0.0, -1.0};
  input.perspective.cameraFocus = {0.0, 0.0, 0.0};
  input.output.width = 20;
  input.output.height = 20;
  return input;
}

std::string ValidTraceJson() {
  return R"({
    "scene": {
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
  })";
}

LoopbackHarness::LoopbackHarness(std::shared_ptr<moonbase::portrait::PortraitHandler> handler,
                                 Wrap wrap)
    : server_(std::make_unique<moonbase::portrait::PortraitServer>(std::move(handler))) {
  handler_ = wrap ? wrap(server_->Handler()) : server_->Handler();
  loopback_ = std::make_shared<smithy::http::Loopback>();
  const auto started = loopback_->Start(handler_);
  if (!started.ok()) {
    ADD_FAILURE() << "loopback start failed: " << started.error().message();
  }
}

moonbase::portrait::PortraitClient LoopbackHarness::MakeClient() {
  smithy::ClientConfig config;
  config.http_client = loopback_;
  auto client = moonbase::portrait::PortraitClient::Create(std::move(config));
  EXPECT_TRUE(client.ok()) << client.error().message();
  return std::move(*client);
}

smithy::http::HttpResponse LoopbackHarness::Send(smithy::http::HttpRequest request) {
  auto response = loopback_->Send(std::move(request));
  if (!response.ok()) {
    ADD_FAILURE() << "loopback send failed: " << response.error().message();
    return {};
  }
  return *response;
}

smithy::http::HttpResponse LoopbackHarness::PostTrace(const std::string& body,
                                                      const std::string& content_type) {
  smithy::http::HttpRequest request;
  request.method = "POST";
  request.target = "/portrait/v1/trace";
  request.headers.Set("content-type", content_type);
  request.body = body;
  return Send(std::move(request));
}

}  // namespace portrait::test_support
