#include "domains/graphics/apis/portrait/tracer_service.h"

#include <exception>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "domains/graphics/libs/png_plusplus/png_plusplus.h"

// trace()'s contract — every failure is a value in the returned StatusOr — is
// implemented by the catch clauses below, so it holds only while throws are
// catchable. MoonBase sets -fno-exceptions nowhere today; under it,
// pngpp::imageToPng's PngException and an allocation failure on a large scene
// would call std::terminate instead of becoming kInternal and
// kResourceExhausted. The try/catch would fail to compile in that build
// anyway, but on a message about exception syntax rather than about the
// contract it silently breaks.
#ifndef __cpp_exceptions
#error \
    "portrait's TracerService converts throws into absl::Status; -fno-exceptions turns \
every such failure into a terminate. Remove the flag, or rewrite trace()'s error contract."
#endif

namespace portrait {
using image_core::Image;
using image_core::RGB_Double;
using std::vector;

std::optional<std::vector<std::uint8_t>> TracerService::lookupCache(const TraceRequest& request) {
  return cache_.get(request);
}

void TracerService::storeInCache(const TraceRequest& request,
                                 const std::vector<std::uint8_t>& png_bytes) {
  cache_.insert(request, png_bytes);
}

absl::StatusOr<TraceResponse> TracerService::trace(TraceRequest& trace_request) {
  auto start_time = std::chrono::steady_clock::now();

  try {
    // Record request counter
    metrics_->RecordCounter("trace_requests_total");

    auto validation_status = validateTraceRequest(trace_request);
    if (!validation_status.ok()) {
      metrics_->RecordCounter("trace_requests_failed", 1, {{"error", "validation_failed"}});
      return validation_status;
    }

    auto cached_png = lookupCache(trace_request);
    if (cached_png.has_value()) {
      recordSceneComplexity(trace_request.scene, /*cache_hit=*/true);
      auto duration = std::chrono::steady_clock::now() - start_time;
      metrics_->RecordLatency("trace_request_duration",
                              std::chrono::duration_cast<std::chrono::microseconds>(duration),
                              {{"cache_hit", "true"}});
      return toResponse(trace_request.output, *cached_png);
    }

    auto [scene, perspective, output] = trace_request;

    recordSceneComplexity(scene, /*cache_hit=*/false);

    auto image = do_trace(scene, perspective, output);
    auto png_bytes = pngpp::imageToPng(image);
    auto traceResponse = toResponse(output, png_bytes);

    // The render is done and the response is built; the cache is only an
    // optimization from here. Storing it copies the PNG again — the peak
    // allocation of the whole call — so a failure here is plausible, and it
    // must not discard an image that already succeeded. Reporting one as
    // "out of memory, try a smaller output" would be actively misleading:
    // the smaller retry re-runs a render that never needed to.
    try {
      storeInCache(trace_request, png_bytes);
    } catch (const std::exception& e) {
      LOG(WARNING) << "portrait: could not cache a completed render: " << e.what();
    }

    auto duration = std::chrono::steady_clock::now() - start_time;
    metrics_->RecordLatency("trace_request_duration",
                            std::chrono::duration_cast<std::chrono::microseconds>(duration),
                            {{"cache_hit", "false"}});

    metrics_->RecordCounter("trace_requests_completed");
    return traceResponse;

  } catch (const std::bad_alloc&) {
    // The one render failure a caller can act on: the same scene at a smaller
    // output size may fit. The message goes on the wire as
    // RenderCapacityError::message, so it says only that.
    //
    // Log before the counter: RecordCounter allocates a map and two strings
    // and may mint an instrument, so under genuine exhaustion it is the more
    // likely of the two to throw, and losing the line would leave nothing.
    LOG(ERROR) << "portrait: render ran out of memory at " << trace_request.output.width << "x"
               << trace_request.output.height;
    metrics_->RecordCounter("trace_requests_failed", 1, {{"error", "out_of_memory"}});
    return absl::ResourceExhaustedError("render exceeded available memory; try a smaller output");
  } catch (const std::exception& e) {
    // what() is for the operator, not the caller: the handler maps kInternal
    // to an unmodeled 500 whose body the generated server fixes at
    // "internal failure", so this line is the only record of the cause.
    LOG(ERROR) << "portrait: render failed: " << e.what();
    metrics_->RecordCounter("trace_requests_failed", 1, {{"error", "rendering_failed"}});
    return absl::InternalError("render failed");
  } catch (...) {
    LOG(ERROR) << "portrait: render failed with a non-std exception";
    metrics_->RecordCounter("trace_requests_failed", 1, {{"error", "rendering_failed"}});
    return absl::InternalError("render failed");
  }
}

void TracerService::recordSceneComplexity(const Scene& scene, bool cache_hit) {
  // Distributions, not gauges: RecordGauge is an up-down delta; absolute
  // per-request counts belong in a histogram.
  //
  // The label matches the one RecordLatency already puts on
  // trace_request_duration, so the two read the same way and a query can join
  // them. Unlabelled, these series were a mean over renders that the dashboard
  // presented as a mean over requests; with it, prom_proxy asks for offered
  // load by summing across the label and for render cost by selecting
  // cache_hit="false" (#1287).
  const std::map<std::string, std::string> labels{{"cache_hit", cache_hit ? "true" : "false"}};
  metrics_->RecordDistribution("scene_sphere_count", static_cast<double>(scene.spheres.size()),
                               labels);
  metrics_->RecordDistribution("scene_light_count", static_cast<double>(scene.lights.size()),
                               labels);
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
