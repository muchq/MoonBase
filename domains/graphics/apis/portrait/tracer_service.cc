#include "domains/graphics/apis/portrait/tracer_service.h"

#include <vector>

#include "domains/graphics/libs/png_plusplus/png_plusplus.h"

namespace portrait {
using image_core::Image;
using image_core::RGB_Double;
using std::vector;

absl::StatusOr<TraceResponse> TracerService::trace(TraceRequest& trace_request) {
  auto start_time = std::chrono::steady_clock::now();

  // Record request counter
  metrics_.RecordCounter("trace_requests_total");

  auto validation_status = validateTraceRequest(trace_request);
  if (!validation_status.ok()) {
    metrics_.RecordCounter("trace_requests_failed", 1, {{"error", "validation_failed"}});
    return validation_status;
  }

  auto cached_png = cache_.get(trace_request);
  if (cached_png.has_value()) {
    auto duration = std::chrono::steady_clock::now() - start_time;
    metrics_.RecordLatency("trace_request_duration",
                           std::chrono::duration_cast<std::chrono::microseconds>(duration),
                           {{"cache_hit", "true"}});
    metrics_.RecordCounter("trace_cache_hits");
    return toResponse(trace_request.output, *cached_png);
  }

  metrics_.RecordCounter("trace_cache_misses");

  try {
    auto [scene, perspective, output] = trace_request;

    // Record scene complexity metrics
    // Distributions, not gauges: RecordGauge is an up-down delta, so
    // feeding it absolute counts accumulated a lifetime sum.
    metrics_.RecordDistribution("scene_sphere_count", static_cast<double>(scene.spheres.size()));
    metrics_.RecordDistribution("scene_light_count", static_cast<double>(scene.lights.size()));

    auto image = do_trace(scene, perspective, output);
    auto png_bytes = pngpp::imageToPng(image);
    auto traceResponse = toResponse(output, png_bytes);
    cache_.insert(trace_request, std::move(png_bytes));

    auto duration = std::chrono::steady_clock::now() - start_time;
    metrics_.RecordLatency("trace_request_duration",
                           std::chrono::duration_cast<std::chrono::microseconds>(duration),
                           {{"cache_hit", "false"}});

    metrics_.RecordCounter("trace_requests_completed");
    return traceResponse;

  } catch (const std::exception& e) {
    metrics_.RecordCounter("trace_requests_failed", 1, {{"error", "rendering_failed"}});
    throw;
  }
}

Image<RGB_Double> TracerService::do_trace(Scene& scene, Perspective& perspective,
                                          const Output& output) {
  auto image = Image<RGB_Double>(output.width, output.height);
  tracy::Scene tracyScene = toTracyScene(scene, output);
  auto [x, y, z] = perspective.cameraPosition;
  const tracy::Vec3 cameraPosition{x, y, z};

  // tracy::Tracer holds unsynchronized RNG state, and trace() runs
  // concurrently under thread-pool transports; each render gets its own.
  tracy::Tracer tracer;
  tracer.drawScene(tracyScene, image, cameraPosition);
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

TraceResponse TracerService::toResponse(const Output& output,
                                        const std::vector<std::uint8_t>& png_bytes) {
  return TraceResponse{
      .png_bytes = png_bytes,
      .width = output.width,
      .height = output.height,
  };
}

}  // namespace portrait
