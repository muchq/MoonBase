#include <algorithm>
#include <iostream>
#include <vector>

#include "cpp/image_core/image_core.h"
#include "cpp/png_plusplus/png_plusplus.h"

constexpr int WIDTH = 800;
constexpr int HEIGHT = 640;

bool isInsideCircle(int x, int y, int cx, int cy, int radius) {
  int dx = x - cx;
  int dy = y - cy;
  return (dx * dx + dy * dy) <= (radius * radius);
}

int main() {
  try {
    using image_core::RGB;

    int centerX = WIDTH / 2;
    int centerY = HEIGHT / 2;
    int radius = std::min(WIDTH, HEIGHT) / 3;

    RGB orange = {255, 165, 0};
    RGB blue = {0, 0, 255};

    std::vector<std::vector<RGB>> image(HEIGHT, std::vector<RGB>(WIDTH));
    for (int y = 0; y < HEIGHT; ++y) {
      for (int x = 0; x < WIDTH; ++x) {
        if (isInsideCircle(x, y, centerX, centerY, radius)) {
          image[y][x] = blue;
        } else {
          image[y][x] = orange;
        }
      }
    }

    pngpp::writePng("output_from_copied_vector.png", image);

    std::cout << "Done.\n";
  } catch (const pngpp::PngException& e) {
    std::cerr << "PNG error: " << e.what() << '\n';
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}