#ifndef CPP_PORTRAIT_TRACER_SERVICE_H
#define CPP_PORTRAIT_TRACER_SERVICE_H

#include <vector>

#include "cpp/image_core/image_core.h"
#include "cpp/tracy/tracy.h"
#include "types.h"

namespace portrait {
class TracerService {
 public:
  image_core::Image<image_core::RGB_Double> trace(Scene &scene, Perspective &perspective,
                                                  const Output &output);

 private:
  tracy::Scene toTracyScene(Scene &scene, const Output &output);
  std::vector<tracy::Sphere> tracify(const std::vector<Sphere> &spheres);
  std::vector<tracy::Light> tracify(const std::vector<Light> &lights);
  tracy::Vec3 tracify(const Vec3 &v);
  tracy::LightType tracify(const LightType &lightType);

  tracy::Tracer tracer_;
};
}  // namespace portrait

#endif