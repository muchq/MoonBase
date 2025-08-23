#ifndef CPP_TRACY_TRACY_H
#define CPP_TRACY_TRACY_H

#include <cmath>
#include <random>
#include <tuple>
#include <vector>

#include "cpp/image_core/image_core.h"

namespace tracy {

using image_core::Image;
using image_core::RGB_Double;

namespace constants {

const RGB_Double UNSET{-1.0};
const RGB_Double BLACK{0.0, 0.0, 0.0};
const RGB_Double DEEP_SPACE{13.0, 12.0, 24.0};
const RGB_Double RED{255.0, 0.0, 0.0};
const RGB_Double EARTHY_BROWN{62.0, 39.0, 35.0};
const RGB_Double DARK_GRAY{45.0, 45.0, 45.0};
const RGB_Double GREEN{0.0, 255.0, 0.0};
const RGB_Double BLUE{0.0, 0.0, 255.0};
const RGB_Double PINK{255.0, 192.0, 203.0};
const RGB_Double YELLOW{255.0, 255.0, 0.0};
const RGB_Double LIGHT{200.0, 201.0, 180.0};
const RGB_Double WHITE{255.0, 255.0, 255.0};

const RGB_Double BACKGROUND = BLACK;

const double INF = std::numeric_limits<double>::infinity();
const double EPSILON = 0.0001;
};  // namespace constants

struct Vec2 {
  double x, y;
};

struct Vec3 {
  double x, y, z;

  Vec3 operator+(Vec3 &o) { return Vec3{.x = x + o.x, .y = y + o.y, .z = z + o.z}; }

  Vec3 operator-(Vec3 &o) { return Vec3{.x = x - o.x, .y = y - o.y, .z = z - o.z}; }

  Vec3 operator*(double o) { return Vec3{.x = x * o, .y = y * o, .z = z * o}; }

  double dot(Vec3 o) { return x * o.x + y * o.y + z * o.z; }

  double length() { return std::sqrt(this->dot(*this)); }
};

struct Sphere {
  Vec3 center;
  double radius;
  RGB_Double color;
  double specular;
  double reflective;
  double r2;  // radius squared cached since we need it a lot

  Sphere(Vec3 _center, double _radius, RGB_Double _color, double _specular, double _reflective)
      : center(_center),
        radius(_radius),
        color(_color),
        specular(_specular),
        reflective(_reflective),
        r2(_radius * _radius) {}
};

enum LightType { AMBIENT, POINT, DIRECTIONAL };

struct Light {
  LightType lightType;
  double intensity;
  Vec3 position;
};

struct Scene {
  double viewportWidth;
  double viewportHeight;
  double projectionPlane;
  RGB_Double backgroundColor;
  double backgroundStarProbability;
  int recursionLimit;
  std::vector<Sphere> spheres;
  std::vector<Light> lights;
};

class Tracer {
 public:
  void drawScene(Scene &scene, Image<RGB_Double> &image, Vec3 cameraPosition);

 private:
  std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<double> dist{0.0, 1.0};

  Vec3 canvasToViewport(Vec2 canvasPoint, Image<RGB_Double> &image, Scene &scene);
  std::tuple<double, double> intersectRaySphere(Vec3 &origin, Vec3 &direction, Sphere &sphere);
  Vec3 reflectRay(Vec3 &normal, Vec3 &ray);
  std::tuple<std::optional<Sphere>, double> closestIntersection(Vec3 &origin, Vec3 &direction,
                                                                double tMin, double tMax,
                                                                std::vector<Sphere> &spheres);
  double specularLightIntensity(Vec3 &normal, Vec3 &ray, Vec3 &view, Light &light, double specular);
  double computeLighting(Vec3 &point, Vec3 &normal, Vec3 &view, Scene &scene, double specular);
  RGB_Double traceRay(Vec3 &origin, Vec3 &direction, double tMin, double tMax, Scene &scene,
                      int recursionDepth);
};

}  // namespace tracy

#endif
