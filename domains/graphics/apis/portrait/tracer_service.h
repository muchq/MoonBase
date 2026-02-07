#ifndef CPP_PORTRAIT_TRACER_SERVICE_H
#define CPP_PORTRAIT_TRACER_SERVICE_H

#include <vector>

#include "absl/status/statusor.h"
#include "domains/platform/libs/futility/cache/lru_cache.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/graphics/libs/image_core/image_core.h"
#include "domains/graphics/libs/tracy_cpp/tracy.h"
#include "types.h"

namespace portrait {
/// Service for rendering 3D ray-traced scenes with result caching.
class TracerService {
 public:
  /// Constructs a TracerService with default cache size of 50.
  explicit TracerService() : cache_(50), metrics_("portrait") {};
  /// Constructs a TracerService with a specified cache size.
  explicit TracerService(uint16_t _cache_size) : cache_(_cache_size), metrics_("portrait") {};
  /// Traces a scene and returns a base64-encoded PNG image.
  absl::StatusOr<TraceResponse> trace(TraceRequest& trace_request);

 private:
  image_core::Image<image_core::RGB_Double> do_trace(Scene& scene, Perspective& perspective,
                                                     const Output& output);
  tracy::Scene toTracyScene(Scene& scene, const Output& output);
  std::vector<tracy::Sphere> tracify(const std::vector<Sphere>& spheres);
  std::vector<tracy::Light> tracify(const std::vector<Light>& lights);
  tracy::Vec3 tracify(const Vec3& v);
  tracy::LightType tracify(const LightType& lightType);
  std::string imageToBase64(const image_core::Image<image_core::RGB_Double>& image);
  TraceResponse toResponse(const Output& output, std::string& base64);

  tracy::Tracer tracer_;
  futility::cache::LRUCache<TraceRequest, std::string> cache_;
  futility::otel::MetricsRecorder metrics_;
};
}  // namespace portrait

#endif
