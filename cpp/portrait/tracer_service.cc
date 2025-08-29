#include "cpp/portrait/tracer_service.h"

#include <vector>

#include "cpp/portrait/base64.h"
#include "cpp/png_plusplus/png_plusplus.h"

namespace portrait {
using image_core::Image;
using image_core::RGB_Double;
using std::vector;

TraceResponse TracerService::trace(TraceRequest& trace_request) {
  auto cached_image = cache_.get(trace_request);
  if (cached_image.has_value()) {
    auto b64Png = cached_image.value();
    return toResponse(trace_request.output, b64Png);
  }

  auto [scene, perspective, output] = trace_request;
  auto image = trace(scene, perspective, output);
  auto b64Png = imageToBase64(image);
  auto traceResponse = toResponse(output, b64Png);
  cache_.insert(trace_request, std::move(b64Png));
  return traceResponse;
}

Image<RGB_Double> TracerService::trace(Scene& scene, Perspective& perspective,
                                       const Output& output) {
  auto image = Image<RGB_Double>(output.width, output.height);
  tracy::Scene tracyScene = toTracyScene(scene, output);
  auto [x, y, z] = perspective.cameraPosition;
  const tracy::Vec3 cameraPosition{x, y, z};

  tracer_.drawScene(tracyScene, image, cameraPosition);
  return image;
}

tracy::Scene TracerService::toTracyScene(Scene& scene, const Output& output) {
  constexpr double viewportWidth = 1.0;
  const double viewportHeight =
      static_cast<double>(output.height) / static_cast<double>(output.width);
  constexpr double projectionPlane = 1.0;

  auto [r, g, b] = scene.backgroundColor;
  const RGB_Double backgroundColor{
      .r = static_cast<double>(r), .g = static_cast<double>(g), .b = static_cast<double>(b)};

  return tracy::Scene{.viewportWidth = viewportWidth,
                      .viewportHeight = viewportHeight,
                      .projectionPlane = projectionPlane,
                      .backgroundColor = backgroundColor,
                      .backgroundStarProbability = scene.backgroundStarProbability,
                      .recursionLimit = 4,
                      .spheres = tracify(scene.spheres),
                      .lights = tracify(scene.lights)};
}

vector<tracy::Sphere> TracerService::tracify(const vector<Sphere>& spheres) {
  vector<tracy::Sphere> tracySpheres;
  for (const auto& [center, radius, color, specular, reflective] : spheres) {
    auto [r, g, b] = color;
    const RGB_Double _color{
        .r = static_cast<double>(r), .g = static_cast<double>(g), .b = static_cast<double>(b)};
    tracySpheres.emplace_back(tracy::Sphere(tracify(center), radius, _color, specular, reflective));
  }
  return tracySpheres;
}

vector<tracy::Light> TracerService::tracify(const vector<Light>& lights) {
  vector<tracy::Light> tracyLights;
  for (const auto& [lightType, intensity, position] : lights) {
    tracyLights.emplace_back(tracy::Light{tracify(lightType), intensity, tracify(position)});
  }
  return tracyLights;
}

tracy::Vec3 TracerService::tracify(const Vec3& v) {
  auto [x, y, z] = v;
  return tracy::Vec3(x, y, z);
}

tracy::LightType TracerService::tracify(const LightType& lightType) {
  return static_cast<tracy::LightType>(lightType);
}

std::string TracerService::imageToBase64(Image<RGB_Double>& image) {
  const std::vector<unsigned char> png_bytes = pngpp::imageToPng(image);
  return pngToBase64(png_bytes);
}

TraceResponse TracerService::toResponse(const Output& output, std::string& base64) {
  return TraceResponse{
      .base64_png = base64,
      .width = output.width,
      .height = output.height,
  };
}

}  // namespace portrait