#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <iostream>
#include <vector>

#include "domains/graphics/libs/image_core/image_core.h"
#include "domains/graphics/libs/png_plusplus/png_plusplus.h"
#include "domains/graphics/libs/tracy_cpp/tracy.h"

using image_core::Image;
using image_core::RGB;
using image_core::RGB_Double;
using tracy::Light;
using tracy::LightType;
using tracy::Scene;
using tracy::Sphere;
using tracy::Vec3;

using std::vector;

void printImage(Image<RGB_Double>& image, const std::string& output_path) {
  pngpp::writePng(output_path, image.toRGB());
}

struct Config {
  int imageWidth = 800;
  int imageHeight = 640;
  std::string inputPath;
  std::string outputPath = "output.png";
  vector<vector<RGB_Double>> pixels;
  RGB_Double backgroundColor = tracy::constants::DEEP_SPACE;
  double starProbability = 0.0006;
};

Config argsToConfig(int argc, char* argv[]) {
  Config config;
  if (argc > 1) {
    config.outputPath = argv[1];
  }

  if (argc < 3) {
    config.pixels.resize(config.imageHeight, vector<RGB_Double>(config.imageWidth));
  } else if (argc == 3) {
    config.inputPath = argv[2];
    auto inputPixels = pngpp::readPng(config.inputPath);
    config.imageHeight = inputPixels.size();
    config.imageWidth = inputPixels[0].size();
    config.backgroundColor = tracy::constants::UNSET;
    config.starProbability = 0.0;

    config.pixels.resize(config.imageHeight, vector<RGB_Double>(config.imageWidth));
    for (int row = 0; row < config.imageHeight; row++) {
      for (int col = 0; col < config.imageWidth; col++) {
        config.pixels[row][col] = inputPixels[row][col].toRGB_Double();
      }
    }
  }
  return config;
}

int main(int argc, char** argv) {
  auto config = argsToConfig(argc, argv);
  if (config.imageWidth <= 0 || config.imageHeight <= 0) {
    std::cerr << "Image size must be greater than zero" << std::endl;
    return 1;
  }

  int imageWidth = config.imageWidth;
  int imageHeight = config.imageHeight;
  double viewportWidth = 1.0;
  double viewportHeight = static_cast<double>(imageHeight) / static_cast<double>(imageWidth);
  double projectionPlane = 1.0;
  Vec3 cameraPosition{0.0, 0.0, -5.0};

  Image<RGB_Double> image = Image<RGB_Double>(config.pixels);

  std::cout << " imageWidth: " << imageWidth << "\n";
  std::cout << "imageHeight: " << imageHeight << "\n";
  std::cout << "  inputPath: " << config.inputPath << "\n";
  std::cout << " outputPath: " << config.outputPath << "\n";

  vector<Sphere> spheres{
      Sphere(Vec3(0.0, -1.0, 3.0), 1.0, tracy::constants::RED, 500.0, 0.55),
      Sphere(Vec3(2.0, 0.0, 4.0), 1.0, tracy::constants::BLUE, 500.0, 0.3),
      Sphere(Vec3(-2.0, 0.0, 4.0), 1.0, tracy::constants::GREEN, 10.0, 0.4),
      Sphere(Vec3(0.0, -5001.0, 8.0), 5000.0, tracy::constants::DARK_GRAY, 1000.0, 0.2)};

  vector<Light> lights{Light(LightType::AMBIENT, 0.2, Vec3{0.0, 0.0, 0.0}),
                       Light(LightType::POINT, 0.6, Vec3{2.0, 1.0, 0.0}),
                       Light(LightType::DIRECTIONAL, 0.2, Vec3{1.0, 4.0, 4.0})};

  Scene scene = Scene{.viewportWidth = viewportWidth,
                      .viewportHeight = viewportHeight,
                      .projectionPlane = projectionPlane,
                      .backgroundColor = config.backgroundColor,
                      .backgroundStarProbability = config.starProbability,
                      .recursionLimit = 4,
                      .spheres = spheres,
                      .lights = lights};
  tracy::Tracer tracer;
  tracer.drawScene(scene, image, cameraPosition);
  printImage(image, config.outputPath);
  return 0;
}
