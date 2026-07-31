#ifndef CPP_PORTRAIT_TRACER_SERVICE_H
#define CPP_PORTRAIT_TRACER_SERVICE_H

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/graphics/libs/image_core/image_core.h"
#include "domains/graphics/libs/tracy_cpp/tracy.h"
#include "domains/platform/libs/futility/cache/lru_cache.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "types.h"

namespace portrait {
/// Service for rendering 3D ray-traced scenes with result caching.
class TracerService {
 public:
  /// Constructs a TracerService with default cache size of 50.
  explicit TracerService() : cache_(50), metrics_("portrait"){};
  /// Constructs a TracerService with a specified cache size.
  explicit TracerService(uint16_t _cache_size) : cache_(_cache_size), metrics_("portrait"){};
  virtual ~TracerService() = default;

  /// Traces a scene and returns the encoded PNG bytes plus dimensions.
  ///
  /// Every failure is a status, including the ones the render path raises as
  /// C++ exceptions — `pngpp::imageToPng` throws `PngException`, and a
  /// 1200x1200 `Image<RGB_Double>` is ~34 MB before `toRGB()` copies it, so
  /// `std::bad_alloc` is reachable under a thread-pool transport. This used
  /// to record a counter and rethrow, which left the transport's
  /// `InvokeHandlerGuarded` to answer with a generic 500 the handler never
  /// got to shape. Out of memory is `kResourceExhausted` (the caller can
  /// retry smaller); anything else is `kInternal`.
  absl::StatusOr<TraceResponse> trace(TraceRequest& trace_request);

 protected:
  /// The render itself. Virtual purely as a testability seam: it shares one
  /// catch with the PNG encode and the cache insert, so a test that makes
  /// this throw exercises the conversion for all of them.
  virtual image_core::Image<image_core::RGB_Double> do_trace(Scene& scene, Perspective& perspective,
                                                             const Output& output);

 private:
  tracy::Scene toTracyScene(Scene& scene, const Output& output);
  std::vector<tracy::Sphere> tracify(const std::vector<Sphere>& spheres);
  std::vector<tracy::Light> tracify(const std::vector<Light>& lights);
  tracy::Vec3 tracify(const Vec3& v);
  tracy::LightType tracify(const LightType& lightType);
  TraceResponse toResponse(const Output& output, const std::vector<std::uint8_t>& png_bytes);

  futility::cache::LRUCache<TraceRequest, std::vector<std::uint8_t>> cache_;
  futility::otel::MetricsRecorder metrics_;
};
}  // namespace portrait

#endif
