#ifndef CPP_PORTRAIT_TRACER_SERVICE_H
#define CPP_PORTRAIT_TRACER_SERVICE_H

#include <vector>

#include "absl/status/statusor.h"
#include "cpp/futility/cache/lru_cache.h"
#include "cpp/futility/otel/metrics.h"
#include "cpp/image_core/image_core.h"
#include "cpp/tracy/tracy.h"
#include "types.h"

namespace portrait {
class TracerService {
 public:
  explicit TracerService() : cache_(50), metrics_("portrait") {};
  explicit TracerService(uint16_t _cache_size) : cache_(_cache_size), metrics_("portrait") {};
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