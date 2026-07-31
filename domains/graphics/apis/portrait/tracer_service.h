#ifndef CPP_PORTRAIT_TRACER_SERVICE_H
#define CPP_PORTRAIT_TRACER_SERVICE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
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
  explicit TracerService() : TracerService(50) {}
  /// Constructs a TracerService with a specified cache size.
  explicit TracerService(uint16_t _cache_size)
      : TracerService(_cache_size, std::make_shared<futility::otel::MetricsRecorder>("portrait")) {}
  /// Takes the recorder so a test can assert what was counted. The failure
  /// counters carry the only label distinguishing an out-of-memory render
  /// from any other, and a label nothing asserts on is one that can silently
  /// become wrong.
  TracerService(uint16_t _cache_size, std::shared_ptr<futility::otel::MetricsRecorder> metrics)
      : cache_(_cache_size), metrics_(std::move(metrics)) {}
  virtual ~TracerService() = default;

  /// Traces a scene and returns the encoded PNG bytes plus dimensions.
  ///
  /// Every failure is a status, including the ones raised as C++ exceptions —
  /// `pngpp::imageToPng` throws `PngException`, and a 1200x1200
  /// `Image<RGB_Double>` is ~34 MB before `toRGB()` copies it, so
  /// `std::bad_alloc` is reachable under a thread-pool transport. This used
  /// to record a counter and rethrow, which left the transport's
  /// `InvokeHandlerGuarded` to answer with a generic 500 the handler never
  /// got to shape. Out of memory is `kResourceExhausted` (the caller can
  /// retry smaller); anything else is `kInternal`.
  ///
  /// The whole body is guarded, not only the render. The cache lookup copies
  /// the stored PNG, so even the cheap hit path can exhaust memory, and an
  /// exception escaping from there would produce a different 500 than the one
  /// the handler shapes while incrementing neither failure counter.
  absl::StatusOr<TraceResponse> trace(TraceRequest& trace_request);

 protected:
  /// The render itself. Virtual purely as a testability seam.
  virtual image_core::Image<image_core::RGB_Double> do_trace(Scene& scene, Perspective& perspective,
                                                             const Output& output);

  /// The two cache operations, virtual for the same reason. Both allocate a
  /// full copy of the PNG, so both are plausible places to run out of memory,
  /// and neither is reachable by throwing from `do_trace` — the store in
  /// particular runs *after* the response already exists, which is exactly
  /// what makes its failure mode different.
  virtual std::optional<std::vector<std::uint8_t>> lookupCache(const TraceRequest& request);
  virtual void storeInCache(const TraceRequest& request,
                            const std::vector<std::uint8_t>& png_bytes);

 private:
  tracy::Scene toTracyScene(Scene& scene, const Output& output);
  std::vector<tracy::Sphere> tracify(const std::vector<Sphere>& spheres);
  std::vector<tracy::Light> tracify(const std::vector<Light>& lights);
  tracy::Vec3 tracify(const Vec3& v);
  tracy::LightType tracify(const LightType& lightType);
  TraceResponse toResponse(const Output& output, const std::vector<std::uint8_t>& png_bytes);

  futility::cache::LRUCache<TraceRequest, std::vector<std::uint8_t>> cache_;
  std::shared_ptr<futility::otel::MetricsRecorder> metrics_;
};
}  // namespace portrait

#endif
