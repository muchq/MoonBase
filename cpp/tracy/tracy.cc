#include "cpp/tracy/tracy.h"

#include <iostream>
#include <random>

#include "cpp/image_core/image_core.h"

namespace tracy {
void Tracer::drawScene(Scene &scene, Image<RGB_Double> &image, Vec3 cameraPosition) {
  for (int x = -image.width / 2; x <= image.width / 2; x++) {
    for (int y = -image.height / 2; y <= image.height / 2; y++) {
      Vec3 direction = canvasToViewport(
          Vec2{.x = static_cast<double>(x), .y = static_cast<double>(y)}, image, scene);
      RGB_Double color =
          traceRay(cameraPosition, direction, 1.0, constants::INF, scene, scene.recursionLimit);
      if (color != constants::UNSET) {
        image.putPixel(x, y, color);
      }
    }
  }
}

Vec3 Tracer::canvasToViewport(Vec2 canvasPoint, Image<RGB_Double> &image, Scene &scene) {
  return Vec3{.x = canvasPoint.x * scene.viewportWidth / image.width,
              .y = canvasPoint.y * scene.viewportHeight / image.height,
              .z = scene.projectionPlane};
}

std::tuple<double, double> Tracer::intersectRaySphere(Vec3 &origin, Vec3 &direction,
                                                      Sphere &sphere) {
  Vec3 originToSphere = origin - sphere.center;
  double a = direction.dot(direction);
  double b = originToSphere.dot(direction) * 2;
  double c = originToSphere.dot(originToSphere) - sphere.r2;

  double discriminant = b * b - (4 * a * c);
  if (discriminant < 0) {
    return {constants::INF, constants::INF};
  }

  double sqrtDiscr = std::sqrt(discriminant);
  double t1 = (-b + sqrtDiscr) / (2 * a);
  double t2 = (-b - sqrtDiscr) / (2 * a);
  return {t1, t2};
}

Vec3 Tracer::reflectRay(Vec3 &normal, Vec3 &ray) { return normal * 2 * normal.dot(ray) - ray; }

std::tuple<std::optional<Sphere>, double> Tracer::closestIntersection(
    Vec3 &origin, Vec3 &direction, double tMin, double tMax, std::vector<Sphere> &spheres) {
  std::optional<Sphere> closestSphere;
  double closestT = constants::INF;

  for (auto &s : spheres) {
    auto [t1, t2] = intersectRaySphere(origin, direction, s);
    if (t1 < closestT && tMin < t1 && t1 < tMax) {
      closestT = t1;
      closestSphere = s;
    }

    if (t2 < closestT && tMin < t2 && t2 < tMax) {
      closestT = t2;
      closestSphere = s;
    }
  }

  return {closestSphere, closestT};
}

double Tracer::specularLightIntensity(Vec3 &normal, Vec3 &ray, Vec3 &view, Light &light,
                                      double specular) {
  if (specular <= 0) {
    return 0.0;
  }

  Vec3 reflectedRay = reflectRay(normal, ray);
  double rDotV = reflectedRay.dot(view);
  if (rDotV <= 0) {
    return 0.0;
  }

  return light.intensity * std::pow(rDotV / reflectedRay.length() * view.length(), specular);
}

double Tracer::computeLighting(Vec3 &point, Vec3 &normal, Vec3 &view, Scene &scene,
                               double specular) {
  double intensity = 0.0;
  for (auto &light : scene.lights) {
    if (light.lightType == LightType::AMBIENT) {
      intensity += light.intensity;
    } else {
      Vec3 ray;
      double tMax = constants::INF;
      if (light.lightType == LightType::POINT) {
        ray = light.position - point;
        tMax = 1.0;
      } else if (light.lightType == LightType::DIRECTIONAL) {
        ray = light.position;
        tMax = constants::INF;
      } else {
        std::cerr << "Unknown light type!" << std::endl;
      }

      auto [shadowSphereMaybe, _] =
          closestIntersection(point, ray, constants::EPSILON, tMax, scene.spheres);

      if (!shadowSphereMaybe.has_value()) {
        double nDotR = normal.dot(ray);
        if (nDotR > 0.0) {
          intensity += (light.intensity * (nDotR / (normal.length() * ray.length())));
        }

        if (specular > 0.0) {
          Vec3 reflectedRay = reflectRay(normal, ray);
          double rDotV = reflectedRay.dot(view);
          if (rDotV > 0.0) {
            intensity += (light.intensity *
                          std::pow(rDotV / (reflectedRay.length() * view.length()), specular));
          }
        }
      }
    }
  }

  return intensity;
}

RGB_Double Tracer::traceRay(Vec3 &origin, Vec3 &direction, double tMin, double tMax, Scene &scene,
                            int recursionDepth) {
  auto [closestSphereMaybe, closestT] =
      closestIntersection(origin, direction, tMin, tMax, scene.spheres);
  if (!closestSphereMaybe.has_value()) {
    if (scene.backgroundStarProbability > 0.0) {
      auto r = dist(rng);
      if (r < scene.backgroundStarProbability) {
        return constants::LIGHT;
      }
    }
    return scene.backgroundColor;
  }

  auto closestSphere = closestSphereMaybe.value();
  Vec3 directionTimesClosestT = direction * closestT;
  Vec3 point = origin + directionTimesClosestT;
  Vec3 n = point - closestSphere.center;
  Vec3 normal = n * (1 / n.length());

  Vec3 view = direction * -1;
  double lighting = computeLighting(point, normal, view, scene, closestSphere.specular);
  RGB_Double localColor = closestSphere.color * lighting;
  if (recursionDepth <= 0 || closestSphere.reflective <= 0) {
    return localColor;
  }

  Vec3 reflectedRay = reflectRay(normal, view);
  RGB_Double reflectedColor =
      traceRay(point, reflectedRay, constants::EPSILON, constants::INF, scene, recursionDepth - 1);
  return localColor * (1.0 - closestSphere.reflective) + reflectedColor * closestSphere.reflective;
}
}  // namespace tracy
